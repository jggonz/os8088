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

; --- window record, 20 bytes, fixed offsets (SPEC.md 11) ---------------------
W_FLAGS equ 0                   ; word: bit0 = used, bit1 = visible,
                                ; bit2 = resizable, bit3 = fullscreen,
                                ; bit4 = content-origin snapped (SPEC.md 11.94)
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
W_MENUS equ 18                  ; word: near ptr to this window's app menu set
                                ; (SPEC.md 12.2), 0 = no menus of its own.
                                ; NOT a template word - wm_create zeroes it;
                                ; a built-in's KD_INIT or a package's
                                ; menu_win_set fills it in. Whichever window
                                ; is frontmost owns the menu bar (SPEC.md 12)
W_DISP  equ 20                  ; dword: the far pointer wm_pkgcall calls to
W_SEG   equ 22                  ; reach this window's procs - {PKG_DISP, the
                                ; owning package's segment}. W_SEG doubles as
                                ; the SEGMENT every near pointer in this
                                ; record is relative to: title, paint, onkey,
                                ; onclick, menus. Zero for a kernel window,
                                ; and zero is what makes the dispatch near
                                ; again for About/Clock/Disk/TaskMgr/Ctrl and
                                ; the file dialog (SPEC.md 11/20.1).
W_ONSIZE equ 24                 ; word: near ptr or 0 - the resize negotiator
                                ; (SPEC.md 11.1). Called BEFORE a new size is
                                ; committed, with SI = window, CX/DX = the
                                ; proposed frame size; it returns the size it
                                ; will accept in CX/DX, which may be the one
                                ; the window already has (a refusal) or
                                ; anything in between. NOT a template word:
                                ; wm_create zeroes it and wm_onsize sets it,
                                ; so no existing window template changes.
WIN_SIZE equ 26                 ; NOT a power of two: never shift, multiply
MAX_WIN  equ 12                 ; mirrored in apps/os88api.inc (SPEC.md 11)

WF_SIZABLE equ 4                ; W_FLAGS bit2: resizable (SPEC.md 11.1)
WF_FULL    equ 8                ; W_FLAGS bit3: fullscreen (SPEC.md 11.2)
WF_SNAP    equ 16               ; W_FLAGS bit4: this window's CONTENT origin
                                ; is kept on a multiple of 8 so its text can
                                ; take font_run's single-store path
                                ; (SPEC.md 11.94/6.1)
WMIN_W     equ 96               ; smallest frame a resize can leave, outer px
WMIN_H     equ 64               ; (SPEC.md 11.1; mirrored in apps/os88api.inc)

; -----------------------------------------------------------------------------
; wm_strseg - point ES at the segment a window's strings live in
; in:  DI = window record
; out: ES = [DI+W_SEG], or KERNEL_SEG when that is 0
; clobbers: ES (the output) and nothing else
;
; W_TITLE, and every string a package's menu set points at, are offsets in
; the OWNER's segment. One place decides which that is.
; -----------------------------------------------------------------------------
wm_strseg:
    push cs
    pop es
    cmp word [di+W_SEG], 0
    je .out
    mov es, [di+W_SEG]
.out:
    ret

; -----------------------------------------------------------------------------
; wm_pkgcall - invoke one of a window's callbacks, wherever its code lives
;
; in:  SI = window record, BP = the callback's offset (non-zero), every other
;      register = that callback's own arguments. SI rather than BX because
;      EVERY callback contract in SPEC.md 11/12.2/38.6 already passes the
;      window in SI - so the dispatch site has nothing to shuffle, and BX
;      stays free for the one callback that wants it (AM_ONCMD's menu set).
; out: whatever the callback returns, CF included
; clobbers: BP (it is the input) - DS and ES are restored
;
; W_SEG = 0 is a kernel window and the call is an ordinary near one. Anything
; else is a package: DS becomes its segment, ES is pointed at KERNEL_SEG (the
; window record it was handed lives there, and so does the file dialog's name
; buffer - a package reads both through ES, SPEC.md 20.5), and the far call
; goes to the DISPATCHER at PKG_DISP in the package's own image, which is a
; three-byte `call bp / retf`. That indirection is what lets every package
; callback stay an ordinary near proc with an ordinary near `ret`: a package
; author never writes `retf`, and a missed one cannot exist.
;
; Re-entrant by construction - the far pointer is read out of the record, not
; out of a global - which it has to be: a package's callback may call
; OSAPI_WM_SHOW, which repaints, which dispatches another package's paint
; proc.
; -----------------------------------------------------------------------------
wm_pkgcall:
    push ds
    push es
    push cs
    pop es                      ; ES = KERNEL_SEG: the record lives here and
                                ; the callback may read it
    cmp word [es:si+W_SEG], 0
    je .near
    mov ds, [es:si+W_SEG]       ; DS = the package's own segment
    call far [es:si+W_DISP]     ; ...and off to its dispatcher
    jmp short .out
.near:
    call bp                     ; a kernel window: DS is already ours
.out:
    pop es
    pop ds
    ret

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
    mov cx, MAX_WIN*WIN_SIZE + MAX_WIN + 1 + MAX_WIN*2
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
; wm_fit - clamp a just-created window's frame onto the live screen
;
; Every window template in the tree - built-in, dialog and package alike -
; was authored against 640x480, and a Hercules screen is 132 rows shorter
; while a CGA one is 280 rows shorter. Rather than make every caller
; adapter-aware (which would mean rebuilding every .o88), the geometry is
; clamped once, here, at the single point all four creation paths funnel
; through. On VGA it is a no-op.
;
; Size first, then position, and the y floor last so it wins: a window taller
; than the desktop is better pinned under the menu bar and clipped at the
; dock than pushed up behind the bar.
; in:       BX -> the window record, W_X..W_H already filled
; out:      W_X/W_Y/W_W/W_H clamped
; clobbers: flags
; -----------------------------------------------------------------------------
wm_fit:
    push ax
    mov ax, [vid_w]             ; w = min(w, screen width)
    cmp [bx+W_W], ax
    jle .wok
    mov [bx+W_W], ax
.wok:
    mov ax, [vid_dock_y0]       ; h = min(h, desktop height) - and ONE PIXEL
    sub ax, MBAR_H + 1          ; SHORT of the dock, because the drop shadow
                                ; lives on row y+h and wm_dock_clear tests
                                ; y+h with `jae`. Clamping to exactly the
                                ; band handed out a window flush against the
                                ; strip, which then made every window opened
                                ; over it pay a dock-under repaint (SPEC.md
                                ; 11.90). This is the one place that decides
                                ; it for every fixed-size package at once
    cmp [bx+W_H], ax
    jle .hok
    mov [bx+W_H], ax
.hok:
    mov ax, [vid_w]             ; x = min(x, screen width - w), floor 0
    sub ax, [bx+W_W]
    cmp [bx+W_X], ax
    jle .xok
    mov [bx+W_X], ax
.xok:
    cmp word [bx+W_X], 0
    jge .xfl
    mov word [bx+W_X], 0
.xfl:
    call wm_snap_win            ; ...and back onto a multiple of 8 if this
                                ; window asked for it (SPEC.md 11.94). After
                                ; both clamps, never before: it moves left,
                                ; which only stays legal once the right edge
                                ; has been dealt with
    mov ax, [vid_dock_y0]       ; y = min(y, dock top - h - 1), floor MBAR_H
    sub ax, [bx+W_H]            ; - the same pixel, for the same reason
    dec ax
    cmp [bx+W_Y], ax
    jle .yok
    mov [bx+W_Y], ax
.yok:
    cmp word [bx+W_Y], MBAR_H
    jge .out
    mov word [bx+W_Y], MBAR_H
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_dock_snap - a window left barely over the dock is nudged back above it
; in:  BX = window ptr (its rect already clamped by the caller)
; out: CF = 1 if W_Y moved, CF = 0 if nothing changed. All registers preserved.
;
; A window's drop shadow is on row y+h, so y+h >= [vid_dock_y0] means it is
; covering the strip - and the strip is drawn UNDER windows, so every later
; dock repaint has to put that window back over it (wm_dock_under, SPEC.md
; 11.90). Landing there by a few pixels is almost always a miss rather than
; an intention, and it is the expensive kind of miss.
;
; TWO gates, and both matter:
;
; - **Less than half the strip.** DOCK_H/2 rows of overlap is the line
;   between "the drag stopped a little low" and "the user put it there".
;   Past that it stays where it was put; the dock goes under it and
;   wm_dock_under pays, which is what that routine is for.
; - **It has to FIT.** Only a window shorter than the desktop band can be
;   moved off the strip at all, and a window that cannot is left exactly
;   alone. That is the case that must not be broken: Paint grown to nearly
;   the whole screen is a deliberate, legal size, and snapping - or worse,
;   refusing - would make the grow box lie about what it does.
;
; MBAR_H is the floor, so this can only ever move a window UP into space it
; already fits in.
; -----------------------------------------------------------------------------
wm_dock_snap:
    push ax
    push cx
    push dx
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]            ; AX = the drop shadow's own row
    mov cx, [vid_dock_y0]
    cmp ax, cx
    jb .no                      ; clear of the strip already
    sub ax, cx
    inc ax                      ; AX = rows of the strip it covers
    cmp ax, DOCK_H / 2
    jae .no                     ; deliberate: leave it, and let the dock go
                                ; under it
    mov dx, cx                  ; the highest y that keeps y+h off the strip
    dec dx
    sub dx, [bx+W_H]
    cmp dx, MBAR_H
    jl .no                      ; it does not fit above the dock at all - a
                                ; grown Paint, and its size is not ours to
                                ; second-guess
    cmp [bx+W_Y], dx
    jle .no                     ; already at or above it
    mov [bx+W_Y], dx
    pop dx
    pop cx
    pop ax
    stc
    ret
.no:
    pop dx
    pop cx
    pop ax
    clc
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
    mov word [bx+W_FLAGS], 1    ; used, not visible
    lea di, [bx+W_X]
    mov cx, 8
    push ds                     ; the template is at ES:SI - the CALLER's
    push es                     ; segment (SPEC.md 11) - and the record is
    pop ds                      ; ours, so swap the two for the movsw and
    pop es                      ; swap them straight back
    cld
    rep movsw                   ; copy the template into x..onclick
    push ds
    push es
    pop ds
    pop es
    mov word [bx+W_MENUS], 0    ; menus are NOT a template word (SPEC 11/12.2)
    mov word [bx+W_ONSIZE], 0   ; ...and neither is the resize negotiator
    mov word [bx+W_DISP], PKG_DISP  ; the far pointer wm_pkgcall uses: the
    mov ax, es                      ; dispatcher's fixed offset, and the
    mov cx, cs                      ; segment the caller called us in. Ours
    cmp ax, cx                      ; means a KERNEL window, which is what
    jne .pkgseg                     ; W_SEG = 0 says (SPEC.md 20.1)
    xor ax, ax
.pkgseg:
    mov [bx+W_SEG], ax
    mov cx, 8                   ; (wm_fit and the index math below want CX
                                ; free; the rep consumed it)
    call wm_fit                 ; templates are authored for 640x480; on a
                                ; smaller screen the record, not the template,
                                ; is the truth (SPEC.md 39.7)
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
; wm_onsize - install (or clear) a window's resize negotiator (SPEC.md 11.1)
; in:  BX = window ptr, AX = near ptr in the window's OWN segment, 0 to clear
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_onsize:
    mov [bx+W_ONSIZE], ax
    ret

; -----------------------------------------------------------------------------
; wm_ask_size - put a proposed size to the window before committing it
; in:  BX = window ptr, CX = proposed width, DX = proposed height
; out: CX/DX = the size the window will accept; every other register
;      preserved
; clobbers: nothing else (flags)
;
; The other half of what docs/PAINT-NOTES.md asked for. wm_resize is how an
; app changes its OWN size; this is how the kernel asks before changing it
; for them - and the answer is a size rather than a yes/no, because the case
; that motivated it (apps/paint refusing to crop artwork) is per-axis: a drag
; that would lose columns but not rows should still get its rows.
;
; A window with no negotiator - every kernel window, and every package that
; never called OSAPI_WM_ONSIZE - answers by leaving CX/DX alone, so the cost
; of the feature to everything that does not use it is one compare.
; -----------------------------------------------------------------------------
wm_ask_size:
    push ax
    push si
    push bp
    cmp word [bx+W_ONSIZE], 0
    je .out
    mov si, bx
    mov bp, [bx+W_ONSIZE]
    call wm_pkgcall             ; SI = window, CX/DX = the proposal
.out:
    pop bp
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_resize - set a window's size from its owner (SPEC.md 11.1)
;
; in:  BX = window ptr, CX = new outer width, DX = new outer height;
;      caller HOLDS the gfx lock (a window callback does)
; out: nothing (all registers preserved); the record carries the clamped
;      size and everything is repainted
;
; The sanctioned version of what apps/paint had to do behind the kernel's
; back (docs/PAINT-NOTES.md): an app that adopts a picture's dimensions has
; to move its own frame, and ui_grow and wm_fullscreen were the only things
; that could. Clamps exactly as a drag does - never smaller than WMIN_W/
; WMIN_H, never past the screen edge or the dock - and then re-fits the
; origin the way ui_drag does, so a window that grew at the right edge slides
; left instead of hanging off it.
; -----------------------------------------------------------------------------
wm_resize:
    push ax
    push cx
    push dx
    cmp cx, WMIN_W
    jae .wok
    mov cx, WMIN_W
.wok:
    mov ax, [vid_w]             ; never wider than the screen
    cmp cx, ax
    jbe .wok2
    mov cx, ax
.wok2:
    cmp dx, WMIN_H
    jae .hok
    mov dx, WMIN_H
.hok:
    mov ax, [vid_dock_y0]       ; ...nor taller than the desktop band
    sub ax, MBAR_H
    cmp dx, ax
    jbe .hok2
    mov dx, ax
.hok2:
    mov [bx+W_W], cx
    mov [bx+W_H], dx
    mov ax, [vid_w]             ; x + w <= screen width
    sub ax, cx
    jns .xc
    xor ax, ax
.xc:
    cmp [bx+W_X], ax
    jle .xs
    mov [bx+W_X], ax
.xs:
    call wm_snap_win            ; the width changed, so the right-edge clamp
                                ; may have moved x (SPEC.md 11.94)
    mov ax, [vid_dock_y0]       ; y + h <= the row the dock owns
    sub ax, dx
    cmp ax, MBAR_H
    jge .yc2
    mov ax, MBAR_H
.yc2:
    cmp [bx+W_Y], ax
    jle .paint
    mov [bx+W_Y], ax
.paint:
    call wm_paint_all
    pop dx
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
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; the rect it is vacating, banked before the
    call wm_win_rect            ; record is cleared out from under us
    test word [si+W_FLAGS], 2
    jnz .vis
    mov ax, 1                   ; already hidden: its pixels went at the
    xor bx, bx                  ; wm_hide, so an empty rect - the chrome is
    xor cx, cx                  ; all this owes (x1 > x2 is the empty test)
    xor dx, dx
    jmp short .bank
.vis:
    test word [si+W_FLAGS], WF_FULL
    jz .bank
    xor ax, ax                  ; fullscreen: the whole screen was its rect
    xor bx, bx
    mov cx, [vid_wm1]
    mov dx, [vid_hm1]
.bank:
    mov [wm_hide_r], ax
    mov [wm_hide_r+2], bx
    mov [wm_hide_r+4], cx
    mov [wm_hide_r+6], dx
    mov bx, si
    call wm_fs_drop             ; a dying fullscreen window must release the
                                ; screen (SPEC.md 11.2)
    mov word [bx+W_FLAGS], 0    ; free the record slot
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov si, ax                  ; orphan the slot's owner entry so a future
    mov byte [wm_owner+si], 0xFF; wm_create reuse starts unowned (SPEC.md 11)
    shl si, 1                   ; ...and its About handler with it, or a
    mov word [wm_about+si], 0   ; reused slot inherits a stranger's (12.2)
    shr si, 1
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
    mov ax, [wm_hide_r]
    mov bx, [wm_hide_r+2]
    mov cx, [wm_hide_r+4]
    mov dx, [wm_hide_r+6]
    call wm_paint_dmg
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_destroy_seg - destroy every window whose callbacks live in segment AX
; in:  AX = a package region's segment (never 0); caller holds the gfx lock
; out: nothing (all registers preserved)
;
; The teardown sweep for windows the loader never bound: a package may
; wm_create more windows than the one it returned to the loader (SPEC.md
; 20.2), and those carry no instance record - only the W_SEG creator stamp
; ties them to the region. Called with the dying instance's I_SPTR
; (app_close_win / inst_task_die / the winless worker death) and with an
; aborted or dying load's region segment, always BEFORE the region is freed:
; an orphan still carries W_SEG/W_DISP into it, and the next repaint would
; far-call whatever claims that memory. main enforced this with the wm_wseg
; side table; the stamp moved into the record (W_SEG) and the sweep moves
; with it.
; -----------------------------------------------------------------------------
wm_destroy_seg:
    push ax
    push bx
    push cx
    mov bx, wm_wins
    mov cx, MAX_WIN
.scan:
    test word [bx+W_FLAGS], 1   ; a used record only - a free slot's W_SEG
    jz .next                    ; is a previous tenant's leftover
    cmp [bx+W_SEG], ax
    jne .next
    call wm_destroy             ; clears the used bit, so a slot can never
                                ; match twice
.next:
    add bx, WIN_SIZE
    loop .scan
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_show - make a window visible, raise it, and draw JUST it (SPEC.md 11.90)
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Showing a window is the one z-order change that REVEALS NOTHING. The
; window lands on top, so every pixel that changes is either its own or
; chrome it does not cover; everything already on screen either stays as it
; is or gets drawn over. That makes the full back-to-front pass avoidable,
; and the pass is expensive: a 640x480 planar desktop dither plus every
; visible window's frame and W_PAINT, to put one window on screen.
;
; Raising an ALREADY visible window is the same argument - it moves up, so
; it can only cover more - which is why this path does not care whether the
; visible bit was already set. wm_front on its own still does the full pass,
; because a raise there is reached from a click that may also have to
; UNCOVER something, and that is genuinely unknown.
;
; Four things change and all four are drawn here:
;   - the menu bar, because menu_activate just handed it to this window;
;   - the dock, because the instance that owns this window may be new;
;   - the OUTGOING front window's title bar, which loses its pinstripes and
;     its two boxes to us (wm_draw_title, drawn BEFORE us so we cover it);
;   - the window itself, drawn last and therefore on top.
;
; The desktop and its drive icons are deliberately not redrawn: showing a
; window cannot change them, and desk_zone_redraw (SPEC.md 26) already owns
; the one thing that can.
;
; **It no longer falls back at all.** A fullscreen window used to send both
; this and wm_front to the full pass, on the grounds that SPEC.md 11.2
; suppresses the chrome and none of the cheap path's reasoning survived that.
; It survives it exactly: a window shown OVER a fullscreen one reveals
; nothing, like any other, and a fullscreen window shown over everything
; reveals nothing because it covers the screen. The chrome is the only part
; that had to change, and wm_raise skips it (wm_fs_vis) rather than the
; caller repainting the world to avoid drawing it. Opening the Standard File
; dialog over a fullscreen player was a whole-screen repaint including that
; player's W_PAINT, twice - once to show the dialog and once to take it away.
; -----------------------------------------------------------------------------
wm_show:
    push ax
    push bx
    push bp
    push di
    mov di, bx                  ; DI = the window being shown
    call wm_top                 ; BX = the OUTGOING front window (0 if none).
    mov bp, bx                  ; BEFORE the visible bit goes on: wm_create
    mov bx, di                  ; already appended us to wm_zord, so once we
    or word [bx+W_FLAGS], 2     ; are visible wm_top answers with US
    mov al, 1                   ; a newly visible window has no pixels on
    call wm_raise               ; screen: it is drawn WHOLE
    pop di
    pop bp
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_fs_vis - is a VISIBLE fullscreen window covering the screen?
; out: CF = 1 yes - the desktop, its drive zones, the dock and the menu bar
;      are all under it and must not be drawn (SPEC.md 11.2). CF = 0 no.
; clobbers: nothing else (flags)
;
; The one question four painters ask, and they used to ask it three different
; ways: wm_paint_all and wm_paint_dmg open-coded it, and the cheap raise path
; treated it as a reason to give up entirely.
; -----------------------------------------------------------------------------
wm_fs_vis:
    push bx
    mov bx, [wm_fs]
    or bx, bx
    jz .no
    test word [bx+W_FLAGS], 2
    jz .no
    pop bx
    stc
    ret
.no:
    pop bx
    clc
    ret

; -----------------------------------------------------------------------------
; wm_fs_zpos - where the fullscreen window sits in wm_zord (module internal)
; in:  SI = wm_zord, DI = one past its last entry; wm_fs_vis has said yes
; out: SI = its slot, or unchanged if it somehow is not in the order
; clobbers: nothing else (flags)
; -----------------------------------------------------------------------------
wm_fs_zpos:
    push ax
    push bx
    push cx
    mov bx, [wm_fs]
    mov ax, bx                  ; AL = its window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; (AH = 0: the pointer is record-aligned)
    mov cx, di
    sub cx, si
    jcxz .out
    push si
.scan:
    cmp [si], al
    je .found
    inc si
    loop .scan
    pop si                      ; not in the order: walk the lot, as before
    jmp short .out
.found:
    add sp, 2
.out:
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_raise - the paint half of "this window is coming to the front"
; in:  BX = DI = the window (its visible bit already set), BP = the window
;      that was frontmost BEFORE this one moved (0 or == DI = there was none),
;      AL = 1 draw the window whole / 0 its title bar is all that changed.
;      Caller holds the gfx lock and has already passed wm_fast_ok.
; out: nothing; AX/BX/BP/DI clobbered (both callers saved them)
;
; Four things change when a window comes to the top and all four are drawn
; here, in this order:
;   - the menu bar, because menu_activate just handed it to this window;
;   - the dock, because the owning instance may be new and because the focus
;     cue moves with the front window (SPEC.md 30);
;   - the OUTGOING front window's title bar, which loses its pinstripes and
;     its two boxes to us - drawn BEFORE us, so we cover it;
;   - this window, drawn last and therefore on top.
;
; The desktop and its drive icons are deliberately not redrawn: coming to the
; front cannot change them, and desk_zone_redraw (SPEC.md 26) already owns the
; one thing that can.
; -----------------------------------------------------------------------------
wm_raise:
    push ax
    call menu_activate          ; BX owns the menu bar from here (SPEC.md 12)
    call wm_lift                ; ...and the top of the z-order
    call wm_fs_vis              ; a fullscreen window is over the chrome, so
    jc .nochrome                ; neither piece of it may be drawn at all
    call menu_draw_bar
    call wm_dock_under          ; the strip, and anything sitting on it
.nochrome:
    cmp bp, di
    je .self                    ; already the front window: no outgoing one
    or bp, bp
    jz .self
    test word [di+W_FLAGS], WF_FULL
    jnz .self                   ; we are about to cover the whole screen: the
                                ; outgoing window's title bar is a strip drawn
                                ; only to be painted over
    xchg bp, di                 ; DI = the outgoing front, BP = the incoming
    call wm_draw_title          ; DI != BP now, so it loses its stripes
    xchg bp, di
.self:
    mov bx, di
    mov bp, di                  ; we ARE the front window now
    pop ax
    or al, al
    jz .titleonly
    jmp wm_draw_win             ; tail call
.titleonly:
    jmp wm_draw_title           ; tail call: DI == BP, so it gains its stripes

; -----------------------------------------------------------------------------
; wm_dock_clear - is the dock strip free of window pixels?
; in:  nothing
; out: CF = 0 = no visible window's frame or drop shadow reaches the dock
; clobbers: nothing else (flags)
;
; wm_fit keeps a window above the dock, but ui_grow's own clamp is looser
; (it stops at the screen edge), so a grown window CAN hang over it. Where
; that happens the dock has to be drawn before that window and the cheap
; path cannot order it, so it declines instead.
; -----------------------------------------------------------------------------
wm_dock_clear:
    push ax
    push bx
    push cx
    push si
    mov si, wm_zord
    mov cl, [wm_zn]
    xor ch, ch
    jcxz .clear
.scan:
    mov al, [si]
    inc si
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .next
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]            ; the drop shadow's own row, y+h
    cmp ax, [vid_dock_y0]
    jae .dirty
.next:
    loop .scan
.clear:
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.dirty:
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; wm_hide - clear the visible bit and repaint what it uncovered
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Hiding DOES reveal, which is why this cannot take wm_show's path - but it
; reveals only inside the rect the window occupied, and every pixel outside
; that rect is already correct. wm_paint_dmg (SPEC.md 11.91) is that argument.
; The rect is taken BEFORE the visible bit drops, because wm_fs_drop can
; restore a fullscreen window's smaller windowed geometry on the way past and
; the pixels to repair are the ones that were actually lit.
; -----------------------------------------------------------------------------
wm_hide:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, bx
    call wm_win_rect            ; AX,BX,CX,DX = the rect it is vacating
    test word [si+W_FLAGS], WF_FULL
    jz .drop
    xor ax, ax                  ; ...so that is the damage, whatever wm_fs_drop
    xor bx, bx                  ; is about to put back in the record
    mov cx, [vid_wm1]
    mov dx, [vid_hm1]
.drop:
    mov [wm_hide_r], ax
    mov [wm_hide_r+2], bx
    mov [wm_hide_r+4], cx
    mov [wm_hide_r+6], dx
    mov bx, si
    call wm_fs_drop             ; a hidden fullscreen window releases the
                                ; screen and re-shows windowed (SPEC.md 11.2)
    and word [bx+W_FLAGS], 0xFFFD
    mov ax, [wm_hide_r]
    mov bx, [wm_hide_r+2]
    mov cx, [wm_hide_r+4]
    mov dx, [wm_hide_r+6]
    call wm_paint_dmg
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

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
; wm_snap - set or clear a window's WF_SNAP bit (public; API 0x0268,
;           SPEC.md 11.94)
; in:  BX = window ptr, AL = 0 clear / non-zero set
; out: nothing - every register preserved, FLAGS INCLUDED (see below)
;
; "My CONTENT origin wants to be a multiple of 8." Not "snap my W_X": the
; frame carries a 1px border, so wm_content returns W_X + 1 and the alignment
; a caller actually needs is one pixel off the frame's. Publishing the frame
; number would make every app rediscover that `inc ax`, and they would each
; get it wrong differently.
;
; It snaps immediately as well as setting the flag, because the intended
; caller is an entry proc or a KD_INIT - before the window is shown, where
; moving it costs nothing and repainting is not owed. Setting it on a visible
; window is legal and moves it at the next paint the caller performs, exactly
; like wm_sizable's grow box.
;
; UI-task context only, same as wm_sizable and for the same reason: every
; W_FLAGS writer runs on the UI task or under the gfx lock.
; -----------------------------------------------------------------------------
wm_snap:
    pushf                       ; FLAGS INCLUDED, and that is not politeness.
                                ; A package's ENTRY PROC returns CF to the
                                ; loader (SPEC.md 20.2) and calls this from
                                ; there, after wm_create's CF has already been
                                ; consumed by a branch - so the carry riding
                                ; in the flags at that point IS the return
                                ; value. wm_snap_ax exits on a `cmp` against
                                ; the screen width, which leaves CF set for
                                ; every window that fits; without this, asking
                                ; to be snapped aborted the launch. On MONO
                                ; only, because that cmp is inside the
                                ; [vid_mono] gate - it loaded perfectly on VGA
    or al, al
    jz .clear
    or word [bx+W_FLAGS], WF_SNAP
    call wm_snap_win
    popf
    ret
.clear:
    and word [bx+W_FLAGS], ~WF_SNAP & 0xFFFF
    popf
    ret

; -----------------------------------------------------------------------------
; wm_snap_win - put this window's content origin back on a multiple of 8
; in:  BX = window ptr, W_X already clamped by the caller
; out: W_X possibly moved; all registers preserved
; -----------------------------------------------------------------------------
wm_snap_win:
    push ax
    mov ax, [bx+W_X]
    call wm_snap_ax
    mov [bx+W_X], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_snap_ax - snap a candidate frame x for a WF_SNAP window
; in:  AX = candidate x (the caller's clamps already applied), BX = window ptr
; out: AX = the x to use; every other register preserved, flags clobbered
;
; The whole of SPEC.md 11.94's mechanism, in one place because four sites
; write W_X and every one of them has to agree.
;
; MONO ONLY, and the gate is [vid_mono] rather than [bb_on] on purpose. The
; two say the same thing on Hercules and CGA, but [bb_on] is also 1 on a VGA
; that has double buffering armed - so gating on it would make windows drag
; differently depending on a Control Panel setting, and buy almost nothing
; for it: the back buffer's flush costs ~24x its render (SPEC.md 32), so the
; render-side saving snapping unlocks is diluted away there.
;
; WF_FULL is left alone: a fullscreen window is at x=0 with no border, so its
; content origin is 0 and already aligned (SPEC.md 11.2) - which is why the
; tracker has had the fast path all along without any of this.
;
; The arithmetic moves LEFT, to the nearest x with x+1 a multiple of 8. Left
; because the caller has already clamped against the right edge and moving
; left cannot violate it. The one exception is x = 0..6, where there is no
; such x at or below it and the answer has to be 7 - the leftmost snapped
; position there is, so a snapped window cannot sit flush against the left
; edge. That is the visible cost of the feature and it is 7 pixels.
; -----------------------------------------------------------------------------
wm_snap_ax:
    test word [bx+W_FLAGS], WF_SNAP
    jz .out
    test word [bx+W_FLAGS], WF_FULL
    jnz .out
    cmp byte [vid_mono], 0
    je .out
    push cx
    mov cx, ax
    inc cx
    and cx, 0xFFF8              ; content left, rounded DOWN to a multiple of 8
    dec cx                      ; ...and back to a frame x
    jns .fits
    add cx, 8                   ; x was 0..6: the next one up is the only one
.fits:
    push ax                     ; only the x=0..6 case can move RIGHT, but the
    mov ax, cx                  ; right-edge test is cheap and it is the one
    add ax, [bx+W_W]            ; way this could hand back an illegal rect
    cmp ax, [vid_w]
    pop ax
    ja .pop                     ; would hang off: leave it unsnapped, and it
    mov ax, cx                  ; simply misses the fast path
.pop:
    pop cx
.out:
    ret

; -----------------------------------------------------------------------------
; wm_sizable - set or clear a window's WF_SIZABLE bit (public; API 0x0108)
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
; wm_fullscreen - enter or leave fullscreen (public; API 0x0110, SPEC.md 11.2)
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
    mov ax, [vid_w]
    mov [bx+W_W], ax
    mov ax, [vid_h]
    mov [bx+W_H], ax
    or word [bx+W_FLAGS], WF_FULL
                                ; Raise it AND draw it WHOLE. Not wm_front:
                                ; that asks wm_obscured whether anything was
                                ; on top of us and draws only the title bar
                                ; when nothing was - a question about the rect
                                ; we just REPLACED. An app already frontmost
                                ; (the usual case - it is responding to its
                                ; own click) took the degenerate chrome-only
                                ; path and its new full-screen content was
                                ; never painted at all. wm_fast_ok used to
                                ; hide this by sending every fullscreen call
                                ; to wm_paint_all; nothing else in the tree
                                ; changes a window's geometry and then raises
                                ; it, which is why wm_front never had to know.
    push bp
    push di
    mov di, bx
    call wm_top                 ; BX = the outgoing front (may be us)
    mov bp, bx
    mov bx, di
    mov al, 1                   ; a new frame has no pixels on screen
    call wm_raise
    pop di
    pop bp
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
; wm_front - raise a window to the front of the z-order (SPEC.md 11.90)
; in:  BX = window ptr (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; Raising REVEALS NOTHING, for exactly the reason showing does not: the window
; moves up, so for every other window the covered area can only grow. What
; changes is this window's own pixels where something used to sit on it, the
; two title bars that trade the pinstripes, and the chrome - never anything
; underneath. So this shares wm_show's path rather than repainting the world.
;
; How much of THIS window has to be drawn is the one extra question, and
; wm_obscured answers it - asked BEFORE wm_lift, while the z-order still says
; what was on top of us. Nothing was: only the title bar changed, so only the
; title bar is drawn. That is the common case, because a click on a background
; window's title bar comes through here.
;
; Raising a window that is already frontmost repaints no window at all. It is
; not a no-op - menu_activate, the bar and the dock still run, because the
; caller may be re-asserting ownership - but the window itself is untouched.
;
; Raising a window that is not visible has no pixels to raise: wm_show is the
; entry point for that, and this declines to the full pass rather than
; silently drawing a hidden window.
; -----------------------------------------------------------------------------
wm_front:
    test word [bx+W_FLAGS], 2
    jz .slow

    push ax
    push bx
    push bp
    push di
    mov di, bx
    call wm_obscured            ; BEFORE wm_lift: was anything on top of us?
    mov al, 0
    jnc .titled
    mov al, 1                   ; yes: whatever it covered has to come back
.titled:
    push ax
    call wm_top                 ; BX = the outgoing front (may be us)
    mov bp, bx
    mov bx, di
    pop ax
    cmp bp, di
    jne .raise
    xor al, al                  ; already frontmost: not even the title bar
    mov bp, di                  ; changes - but wm_raise still owes the bar
.raise:                         ; and the dock, so it is not skipped
    cmp bp, di
    jne .go
    or al, al
    jnz .go
    call menu_activate          ; the degenerate case: chrome only
    call wm_lift
    call wm_fs_vis              ; ...and under a fullscreen window, not even
    jc .out                     ; that
    call menu_draw_bar
    call wm_dock_under
    jmp short .out
.go:
    call wm_raise
.out:
    pop di
    pop bp
    pop bx
    pop ax
    ret
.slow:
    call menu_activate          ; BX owns the menu bar from here (SPEC.md 12)
    call wm_lift
    jmp wm_paint_all            ; tail call; same register contract

; -----------------------------------------------------------------------------
; wm_lift - move a window to the top of the z-order, drawing nothing
; in:  BX = window ptr
; out: nothing (all registers preserved)
;
; Split out of wm_front so wm_show can reorder without committing to the
; full repaint that used to be welded to it.
; -----------------------------------------------------------------------------
wm_lift:
    push ax
    push cx
    push si
    mov ax, bx                  ; AL = window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    jcxz .out                   ; empty z-order: nothing to reorder
    mov si, wm_zord
.find:
    cmp [si], al
    je .lift
    inc si
    loop .find
    jmp short .out              ; not found (cannot happen for a used window)
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
.out:
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

; 1 << n for a window index - MAX_WIN is 12, an 8086 has no variable shift
; worth the code, and this is read once per window per damage repaint.
wm_bit1: dw 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048

; -----------------------------------------------------------------------------
; wm_win_rect - a window's occupied rect, drop shadow included
; in:  SI = window ptr
; out: AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; clobbers: the four outputs only
; -----------------------------------------------------------------------------
wm_win_rect:
    mov ax, [si+W_X]
    mov bx, [si+W_Y]
    mov cx, ax
    add cx, [si+W_W]
    mov dx, bx
    add dx, [si+W_H]
    test word [si+W_FLAGS], WF_FULL
    jz .out
    dec cx                      ; fullscreen draws no shadow (SPEC.md 11.2)
    dec dx
.out:
    ret

; -----------------------------------------------------------------------------
; wm_dmg_hit - does the rect in AX,BX,CX,DX overlap the damage rect?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive); [wm_dmg_*] set
; out: CF = 1 they overlap; all registers preserved
; -----------------------------------------------------------------------------
wm_dmg_hit:
    cmp ax, [wm_dmg_x2]
    jg .no
    cmp cx, [wm_dmg_x1]
    jl .no
    cmp bx, [wm_dmg_y2]
    jg .no
    cmp dx, [wm_dmg_y1]
    jl .no
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; wm_dmg_add - grow the damage rect to cover AX,BX,CX,DX as well
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
wm_dmg_add:
    cmp ax, [wm_dmg_x1]
    jge .x1
    mov [wm_dmg_x1], ax
.x1:
    cmp bx, [wm_dmg_y1]
    jge .y1
    mov [wm_dmg_y1], bx
.y1:
    cmp cx, [wm_dmg_x2]
    jle .x2
    mov [wm_dmg_x2], cx
.x2:
    cmp dx, [wm_dmg_y2]
    jle .y2
    mov [wm_dmg_y2], dx
.y2:
    ret

; -----------------------------------------------------------------------------
; wm_win_hit - do two windows' occupied rects overlap?
; in:  SI = window A, DI = window B
; out: CF = 1 they overlap; all registers preserved
; Both rects include the 1px drop shadow, exactly as wm_obscured tests them.
; -----------------------------------------------------------------------------
wm_win_hit:
    push ax
    push dx
    mov ax, [si+W_X]            ; A.left > B.right?
    mov dx, [di+W_X]
    add dx, [di+W_W]
    cmp ax, dx
    jg .no
    mov ax, [si+W_X]            ; A.right < B.left?
    add ax, [si+W_W]
    cmp ax, [di+W_X]
    jl .no
    mov ax, [si+W_Y]            ; A.top > B.bottom?
    mov dx, [di+W_Y]
    add dx, [di+W_H]
    cmp ax, dx
    jg .no
    mov ax, [si+W_Y]            ; A.bottom < B.top?
    add ax, [si+W_H]
    cmp ax, [di+W_Y]
    jl .no
    pop dx
    pop ax
    stc
    ret
.no:
    pop dx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; wm_dmg_union - grow a rect to cover a window's occupied rect as well
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive), SI = window ptr
; out: the same four, grown; SI preserved
;
; For the one caller that has to name two rects at once: ui_drag knows where
; the window was and the record knows where it is, and wm_paint_dmg wants one
; rect. Separate from wm_dmg_add because that one edits the armed damage rect
; and this runs BEFORE there is one.
; -----------------------------------------------------------------------------
wm_dmg_union:
    push di
    mov di, [si+W_X]
    cmp di, ax
    jge .x1
    mov ax, di
.x1:
    mov di, [si+W_Y]
    cmp di, bx
    jge .y1
    mov bx, di
.y1:
    mov di, [si+W_X]
    add di, [si+W_W]            ; the drop shadow's own column
    cmp di, cx
    jle .x2
    mov cx, di
.x2:
    mov di, [si+W_Y]
    add di, [si+W_H]
    cmp di, dx
    jle .y2
    mov dx, di
.y2:
    pop di
    ret

; -----------------------------------------------------------------------------
; wm_paint_chrome - the dock and the menu bar, and nothing else
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; What is left to draw when a window that was ALREADY hidden goes away: its
; pixels left the screen at the wm_hide, so nothing is revealed - but the dock
; loses a tile and the menu bar may lose its owner. That is the second half of
; closing a task-owned app (SPEC.md 29), where the close box hides and the
; worker destroys a moment later; the destroy used to repaint the whole screen
; a second time for two strips' worth of change.
;
; The dock strip is drawn UNDER windows (SPEC.md 30), which wm_dock_under
; owns: it puts back whatever window is sitting on the pixels it just drew,
; and does nothing at all when the strip did not change.
; -----------------------------------------------------------------------------
wm_paint_chrome:
    call wm_dock_under
    jmp menu_draw_bar           ; tail call

; -----------------------------------------------------------------------------
; wm_paint_dmg - repaint only what a rectangle's worth of change touched
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive; may hang off the screen)
;      caller holds the gfx lock
; out: nothing (all registers preserved)
;
; The counterpart to wm_show's argument (SPEC.md 11.90). SHOWING a window
; reveals nothing, so it costs one window; HIDING, destroying or MOVING one
; does reveal - but only inside the rect it vacated, and everything outside
; that rect is already correct on screen. A window on the right of the screen
; therefore does not redraw because a window on the left closed.
;
; What is drawn, in wm_paint_all's order so the layering is identical:
;   1. the desktop dither, clipped to the damage rect;
;   2. every drive zone the rect touches, whole (SPEC.md 26);
;   3. the dock and the menu bar, always - both carry state that a hide or a
;      destroy just changed (a tile leaves, or the focus cue moves);
;   4. every window that needs it, back to front.
;
; "Needs it" is a two-part rule, and the second part is what makes it safe.
; A window is marked if it overlaps the damage rect - and ALSO if it overlaps
; a window already marked below it, because that window is redrawn WHOLE and
; would otherwise paint over this one. Marking runs bottom-to-top, so one
; pass reaches the whole transitive closure.
;
; A touched drive zone is folded into the rect before the marking pass rather
; than special-cased in it: it is drawn whole, so a window over it has to be
; redrawn, and growing the rect is what makes the marking notice. The DOCK is
; not folded in, and that is not symmetry for its own sake - the strip is
; full width, so a rect grown to it erases the whole width for the damage's
; entire height, which took the drive icons out from under a window that
; merely reached the bottom of the screen. It is a per-window test instead.
; -----------------------------------------------------------------------------
wm_paint_dmg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov byte [wm_dmg_fs], 0     ; a visible fullscreen window covers the
    call wm_fs_vis              ; desktop, its drive zones, the dock and the
    jnc .clamp                  ; bar - so none of those is drawn, and the
    mov byte [wm_dmg_fs], 1     ; window pass is the whole of the work

.clamp:
    or ax, ax                   ; onto the live screen (SPEC.md 39.2)
    jns .cx1
    xor ax, ax
.cx1:
    or bx, bx
    jns .cy1
    xor bx, bx
.cy1:
    cmp cx, [vid_wm1]
    jle .cx2
    mov cx, [vid_wm1]
.cx2:
    cmp dx, [vid_hm1]
    jle .cy2
    mov dx, [vid_hm1]
.cy2:
    cmp ax, cx
    jg .chrome                  ; nothing was uncovered, but a caller with an
    cmp bx, dx                  ; empty rect is still telling us SOMETHING
    jg .chrome                  ; changed - the dock's tiles, the bar's owner
    mov [wm_dmg_x1], ax
    mov [wm_dmg_y1], bx
    mov [wm_dmg_x2], cx
    mov [wm_dmg_y2], dx

    cmp byte [wm_dmg_fs], 0
    jne .wins                   ; fullscreen: no zones to grow the rect to
    call desk_dmg_zones         ; AL = the zone mask; the rect grows to cover
    mov [wm_dmg_zn], al         ; every zone it touched

.fill:
    mov ax, [wm_dmg_x1]         ; the desktop dither, clipped to the damage
    mov bx, [wm_dmg_y1]         ; and to the band below the menu bar
    cmp bx, MBAR_H
    jge .fy
    mov bx, MBAR_H
.fy:
    mov cx, [wm_dmg_x2]
    mov dx, [wm_dmg_y2]
    cmp bx, dx
    jg .zones
    call gfx_fill_gray
.zones:
    mov al, [wm_dmg_zn]
    call desk_paint_mask        ; the touched drive zones, whole
    mov ax, [wm_dmg_y2]         ; reached the strip? then the dither above
    cmp ax, [vid_dock_y0]       ; just ran over it, and whatever WAS covering
    jl .dock                    ; it has moved - the one case that owes the
    call dock_force             ; dock a whole redraw rather than a tile
.dock:
    call dock_paint
    call menu_draw_bar
.wins:
    call wm_dmg_wins            ; mark and draw; leaves wm_dmg_mask for below

    ; --- the window this uncovered is the front window now -----------------
    ; Hiding or destroying the frontmost window promotes whatever was under
    ; it, and the promotion is visible: the pinstripes, the close box and the
    ; minimize box belong to the front window alone (SPEC.md 11). If the new
    ; front was redrawn above it already has them; if it sat well clear of
    ; the damage it was never touched, and this is the one rect it owes.
.promote:
    call wm_top
    or bx, bx
    jz .out
    mov ax, bx
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = its index (AH = 0: record-aligned)
    xor ah, ah
    mov di, ax
    shl di, 1
    mov ax, [wm_bit1+di]
    test [wm_dmg_mask], ax
    jnz .out                    ; redrawn in this pass: it has them already
    mov di, bx
    mov bp, bx                  ; DI == BP: draw it as the FRONT window
    call wm_draw_title
    jmp short .out

.chrome:
    cmp byte [wm_dmg_fs], 0     ; "nothing was uncovered but the chrome
    jne .out                    ; changed" - and under a fullscreen window
    call wm_paint_chrome        ; the chrome is not on screen to change
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ...and the pass itself, which wm_dock_under borrows to put the windows back
; over a strip it has just redrawn.
; -----------------------------------------------------------------------------
; wm_dmg_wins - redraw every window that a damage rect touched, back to front
; in:  wm_dmg_x1/y1/x2/y2 set; caller holds the gfx lock
; out: wm_dmg_mask = which windows were drawn. All registers preserved.
; -----------------------------------------------------------------------------
wm_dmg_wins:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    ; --- mark: overlaps the damage, or overlaps something already marked ---
    ; Bottom to top, so "already marked" means "marked and below us" and one
    ; pass reaches the whole transitive closure. Nothing here may keep a loop
    ; counter in a general register: wm_win_rect writes all four of them.
    mov word [wm_dmg_mask], 0
    xor bp, bp                  ; BP = index into wm_zord
.mark:
    mov al, [wm_zn]
    xor ah, ah
    cmp bp, ax
    jae .draw
    mov si, wm_zord
    add si, bp
    mov al, [si]
    call wm_idx2ptr             ; BX = window ptr (AX and CX survive)
    test word [bx+W_FLAGS], 2
    jz .mnext
    mov si, bx                  ; SI = our window for the rest of the pass
    call wm_win_rect            ; AX,BX,CX,DX = our rect, shadow included
    call wm_dmg_hit
    jc .mset
    cmp dx, [vid_dock_y0]       ; the dock is repainted unconditionally above
    jge .mset                   ; and it is drawn UNDER windows, so a window
                                ; reaching into the strip is damaged by it -
                                ; per window, rather than by widening the
                                ; rect, which would erase the whole width
    mov word [wm_dmg_j], 0      ; not damaged directly - but a marked window
.mbelow:                        ; below is redrawn WHOLE and would paint on us
    mov ax, [wm_dmg_j]
    cmp ax, bp
    jae .mnext
    mov di, wm_zord
    add di, ax
    mov al, [di]                ; AL = that window's index
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [wm_bit1+bx]
    test [wm_dmg_mask], bx
    jz .mbnext
    call wm_idx2ptr             ; BX = its record (AL still its index)
    mov di, bx
    call wm_win_hit             ; SI = ours, DI = theirs; preserves both
    jc .mset
.mbnext:
    inc word [wm_dmg_j]
    jmp short .mbelow
.mset:
    mov ax, si                  ; AL = our window index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; (AH = 0: a window ptr is record-aligned)
    xor ah, ah
    mov di, ax
    shl di, 1
    mov ax, [wm_bit1+di]
    or [wm_dmg_mask], ax
.mnext:
    inc bp
    jmp .mark

    ; --- draw the marked ones, back to front ------------------------------
.draw:
    cmp word [wm_dmg_mask], 0
    je .wdone
    call wm_top
    mov bp, bx                  ; BP = frontmost visible (0 if none)
    xor si, si                  ; SI = index into wm_zord
.dwin:
    mov al, [wm_zn]
    xor ah, ah
    cmp si, ax
    jae .wdone
    mov di, wm_zord
    add di, si
    mov al, [di]
    mov bl, al
    xor bh, bh
    shl bx, 1
    mov bx, [wm_bit1+bx]
    test [wm_dmg_mask], bx
    jz .dnext
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .dnext
    call wm_covered             ; wholly hidden: every pixel it would write
    jc .dnext                   ; is written again by something above it
    call wm_draw_win
.dnext:
    inc si
    jmp .dwin
.wdone:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_dock_under - bring the dock strip up to date, and put back whatever
;                 window is sitting on it
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved)
;
; The strip is drawn UNDER windows (SPEC.md 30), and wm_fit only keeps a
; window above it - ui_grow's clamp is looser, so a grown window CAN hang over
; it. This used to be the cheap raise path's one veto: wm_fast_ok declined and
; the caller repainted the whole screen, so with a single oversized window on
; screen EVERY focus change, show and un-minimize cost a full repaint.
;
; It costs a rectangle instead. Two tests come first and both are usually no:
; dock_paint says whether it drew anything at all, and wm_dock_clear whether
; any window is over the strip. Only when both are yes does anything get
; redrawn, and then only the windows the strip's own rect touches.
; -----------------------------------------------------------------------------
wm_dock_under:
    push ax
    call dock_paint             ; CF = 1 if it put pixels on the strip
    jnc .out
    call wm_dock_clear          ; CF = 1 if a window is sitting on them
    jnc .out
    xor ax, ax
    mov [wm_dmg_x1], ax
    mov ax, [vid_dock_y0]
    mov [wm_dmg_y1], ax
    mov ax, [vid_wm1]
    mov [wm_dmg_x2], ax
    mov ax, [vid_hm1]
    mov [wm_dmg_y2], ax
    call wm_dmg_wins
.out:
    pop ax
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
    call wm_fs_vis              ; a visible fullscreen window covers the
    jc .fswins                  ; desktop, dock and menu bar - painting
                                ; them under it is pure waste (SPEC.md 11.2)
    xor ax, ax                  ; desktop: 50% dither under the bar
    mov bx, MBAR_H
    mov cx, [vid_wm1]
    mov dx, [vid_hm1]
    call gfx_fill_gray
    call desk_paint             ; drive icons live on the desktop, under
                                ; every window (SPEC.md 26)
    call dock_force             ; the dither just ran over the strip, and it
    call dock_paint             ; is drawn UNDER windows (SPEC.md 30)
    call menu_force
    call menu_draw_bar
.wins:
    call wm_top
    mov bp, bx                  ; BP = frontmost visible (0 if none)
    mov si, wm_zord
    mov al, [wm_zn]
    mov ah, 0
    mov di, si
    add di, ax                  ; DI = one past the front
    jmp short .win

    ; --- ...and under a fullscreen window the walk STARTS at it ------------
    ; Everything below a window covering the whole screen is covered by it,
    ; entirely and by construction. wm_covered would reach the same answer
    ; one region-arithmetic pass per window later; this is the same fact
    ; known in advance, and it is what stops a fullscreen player from
    ; examining - and, before wm_covered existed, redrawing - every window
    ; parked behind it.
.fswins:
    call wm_top
    mov bp, bx                  ; BP = frontmost visible
    mov si, wm_zord
    mov al, [wm_zn]
    mov ah, 0
    mov di, si
    add di, ax                  ; DI = one past the front
    call wm_fs_zpos             ; SI = the fullscreen window's slot in wm_zord
.win:
    cmp si, di
    je .done
    mov al, [si]
    inc si
    call wm_idx2ptr
    test word [bx+W_FLAGS], 2
    jz .win
    call wm_covered             ; a window nothing shows of writes no pixel
    jc .win                     ; that a window above it does not overwrite
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
; wm_about_set - register a window's About handler (public; API 0x01E0,
;                SPEC.md 12.2)
; in:  BX = window ptr, SI = the handler's offset in the CALLER's segment
;      (0 = this window has no About)
; out: nothing - every register preserved, FLAGS INCLUDED
;
; The whole of the opt-in for the name menu. A window that declares a handler
; gets its application's NAME in the bar as a real pull-down carrying one
; item, 'About <Name>'; picking it dispatches this offset exactly like
; W_ONCLICK - through the window's own dispatcher, so a package writes an
; ordinary near proc ending in `ret` (SPEC.md 20.1). A window that declares
; none keeps the plain label the bar has always drawn, which is why every
; existing application is unaffected and no package can be handed a menu
; index it was never written for.
;
; STORED, never dereferenced here - like menu_win_set, whose flag-preserving
; contract this shares for the same reason: the one place it is meant to be
; called from is a package entry proc sitting between wm_create and its own
; `ret`, which owes the loader wm_create's CF (SPEC.md 20.2).
; -----------------------------------------------------------------------------
wm_about_set:
    pushf
    push ax
    push di
    call wm_ptr2idx             ; AX = window index (AH = 0)
    mov di, ax
    shl di, 1
    mov [wm_about+di], si
    cmp bx, [menu_win]
    jne .out
    call menu_relayout          ; the active application's bar changed
.out:
    pop di
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; wm_about_get - a window's About handler offset, 0 = none (module internal
;                to the bar: menu_relayout and ui_dispatch)
; in:  BX = window ptr
; out: AX = the offset wm_about_set registered, 0 = none
; clobbers: flags only
; -----------------------------------------------------------------------------
wm_about_get:
    push di
    call wm_ptr2idx             ; AX = window index (AH = 0)
    mov di, ax
    shl di, 1
    mov ax, [wm_about+di]
    pop di
    ret

; -----------------------------------------------------------------------------
; wm_geom - a window's CONTENT size, and whether it is on screen (public;
;           API 0x01B0, SPEC.md 11)
; in:  BX = window ptr
; out: CX = content width, DX = content height, both in pixels;
;      CF = 1 the window is not visible (CX/DX are still filled in).
;      Everything else preserved.
;
; The companion to wm_content, which answers the origin: together they are
; the whole content rect. A package can read W_W/W_H out of the
; record itself (ES = KERNEL_SEG in every callback, SPEC.md 20.1) and most
; of them do - but that is FRAME size, and every caller then repeats the
; same `sub 2` / `sub TITLE_H+1`. This is that arithmetic in one place, so
; a package never repeats it and never gets it subtly wrong.
;
; Callable from any context, lock held or not: it reads four words. The
; answer is only STABLE under the lock, which is when painters ask.
; -----------------------------------------------------------------------------
wm_geom:
    mov cx, [bx+W_W]
    mov dx, [bx+W_H]
    test word [bx+W_FLAGS], WF_FULL
    jnz .vis                    ; fullscreen: the frame IS the content
    sub cx, 2                   ; content cols x+1 .. x+w-2 (SPEC.md 11)
    sub dx, TITLE_H + 1         ; content rows y+TITLE_H .. y+h-2
.vis:
    test word [bx+W_FLAGS], 2   ; visible?
    jz .hidden
    clc
    ret
.hidden:
    stc
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

; =============================================================================
; The clip region (SPEC.md 11.3) - what a background task may draw
;
; wm_obscured above answers a boolean, and every background painter used it
; as a veto: one covered pixel and the whole frame was skipped, because the
; gfx_* primitives draw in absolute screen coordinates and would otherwise
; paint straight over the window on top. The region replaces the veto: it is
; the window's content rect with every visible window above it subtracted,
; and while it is armed the six clipped primitives (SPEC.md 5/6/25) draw
; only inside it.
;
; Three properties are binding:
;   - It is built and consumed inside ONE gfx lock hold. wm_zord and the
;     window rects are mutated only under the lock, so a region built at
;     wm_clip_set is valid until gfx_unlock - and stale the moment after,
;     which is why gfx_unlock clears it rather than trusting callers.
;   - Overflow degrades to "skip the frame" (CF = 1), which is exactly the
;     old wm_obscured behaviour and therefore cannot regress anything.
;   - Nothing here needs an interrupt guard, for the same reason vga12's
;     rect scratch does not: the mouse ISR never draws while the lock is set.
;
; There is no wm_obscured fast path in wm_clip_set, deliberately: the region
; build IS wm_obscured's z-order walk plus a rect subtraction that does
; nothing when nothing overlaps, so a pre-test would only walk it twice.
; =============================================================================

; WM_CLIP_MAX and the WCR_* rect offsets are in kernel.asm: vga12.inc,
; font.inc and icons.inc read this list and NASM sees them first.

wm_dmg_x1   dw 0                ; the damage rect wm_paint_dmg works from,
wm_dmg_y1   dw 0                ; inclusive and already clamped to the screen
wm_dmg_x2   dw 0
wm_dmg_y2   dw 0
wm_dmg_mask dw 0                ; bit n = window n must be redrawn
wm_dmg_zn   db 0                ; bit n = drive zone n must be redrawn
wm_dmg_fs   db 0                ; 1 = a fullscreen window is over the chrome,
                                ; so this pass draws windows and nothing else
wm_dmg_j    dw 0                ; the marking pass's inner index
wm_hide_r   times 4 dw 0        ; wm_hide/wm_destroy: the vacated rect,
                                ; banked across the record teardown

wm_clip_n   dw 0                ; rects in wm_clip_tab; 0 = disarmed.
                                ; Initialized data, NOT .bss: every gfx_fill
                                ; reads it, including the machine's first,
                                ; and -f bin zeroes nothing.

; -----------------------------------------------------------------------------
; wm_clip_set - arm clipping to a window's visible region
; in:  BX = window ptr; caller HOLDS the gfx lock
; out: CF = 1 the window is entirely invisible - nothing armed, skip the
;      frame; CF = 0 clipping is armed and dies at the next gfx_unlock
; clobbers: flags
; -----------------------------------------------------------------------------
wm_clip_set:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [wm_clip_n], 0

    mov ax, [bx+W_X]            ; seed with the content rect - the same one
    mov dx, [bx+W_Y]            ; wm_draw_win white-fills before W_PAINT
    mov cx, ax
    add cx, [bx+W_W]
    dec cx
    mov si, dx
    add si, [bx+W_H]
    dec si
    test word [bx+W_FLAGS], WF_FULL
    jnz .seed                   ; fullscreen: the frame IS the content
    inc ax                      ; else x+1 .. x+w-2, y+TITLE_H .. y+h-2
    dec cx
    add dx, TITLE_H
    dec si
.seed:
    cmp ax, cx
    jg .none                    ; degenerate content rect
    cmp dx, si
    jg .none
    mov [wm_clip_tab+WCR_X1], ax
    mov [wm_clip_tab+WCR_Y1], dx
    mov [wm_clip_tab+WCR_X2], cx
    mov [wm_clip_tab+WCR_Y2], si
    mov word [wm_clip_n], 1

    call wm_clip_occl           ; subtract every visible window above us
    jc .none                    ; the list overflowed: degrade to a skip
    cmp word [wm_clip_n], 0
    jne .done
.none:
    mov word [wm_clip_n], 0
    stc
    jmp short .out
.done:
    clc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_occl - subtract every visible window ABOVE BX from the armed list
; in:  BX = window ptr; the list holds at least one rect
; out: CF = 1 the list overflowed - nothing about it is valid. CF = 0 and
;      [wm_clip_n] = 0 means the seed rect is entirely covered.
; clobbers: flags
;
; The walk both callers need, and the reason they can disagree about what an
; empty list MEANS: wm_clip_set reads it as "invisible, skip the frame" and
; wm_covered as "covered, skip the redraw" - opposite readings of the same
; fact, and opposite degradations on overflow (skip vs. draw anyway).
; -----------------------------------------------------------------------------
wm_clip_occl:
    push ax
    push bx
    push cx
    push si
    mov ax, bx                  ; AL = our index
    sub ax, wm_wins
    mov cl, WIN_SIZE
    div cl                      ; AL = index (AH = 0: ptr is record-aligned)
    mov ch, 0
    mov cl, [wm_zn]
    mov si, wm_zord
    jcxz .done
.find:
    cmp [si], al
    je .found
    inc si
    loop .find
    jmp short .done             ; not in z-order (cannot happen)
.found:
    inc si                      ; first entry above us
    dec cx
.scan:
    jcxz .done
    dec cx
    mov al, [si]
    inc si
    call wm_idx2ptr             ; BX = window above (AX and CX preserved)
    test word [bx+W_FLAGS], 2
    jz .scan
    call wm_clip_sub            ; subtract it, shadow included
    jc .over
    cmp word [wm_clip_n], 0
    jne .scan
.done:
    clc
    jmp short .out
.over:
    stc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_covered - is EVERY pixel of this window hidden by windows above it?
; in:  BX = window ptr (visible); caller HOLDS the gfx lock
; out: CF = 1 = wholly covered: drawing it would change no pixel on screen.
;      All registers preserved, and the clip list is left DISARMED either way.
;
; The same region arithmetic wm_clip_set does, seeded with the FRAME rect
; (drop shadow included) instead of the content rect, because a title bar
; peeking out from under another window is still a pixel this window owns.
;
; A back-to-front painter may skip a window this answers yes about: every
; pixel it would write is written again by a window later in wm_zord, whether
; that window is being redrawn in this pass or is merely still on screen. The
; consequence a package author can see is that **W_PAINT does not run on a
; wholly covered window**, so a paint proc must be a repaint and nothing else
; - anything it also does has to tolerate being skipped (SPEC.md 11.91).
;
; Overflow degrades the OTHER way from wm_clip_set: more than 16 fragments
; means "not covered", i.e. draw it. Skipping on a maybe would lose pixels.
; -----------------------------------------------------------------------------
wm_covered:
    push ax
    push cx
    push dx
    push si
    mov word [wm_clip_n], 0
    mov ax, [bx+W_X]
    mov dx, [bx+W_Y]
    mov cx, ax
    add cx, [bx+W_W]            ; x .. x+w and y .. y+h: the frame plus the
    mov si, dx                  ; 1px drop shadow, which wm_draw_win draws
    add si, [bx+W_H]
    test word [bx+W_FLAGS], WF_FULL
    jz .seed
    dec cx                      ; fullscreen has no shadow (SPEC.md 11.2)
    dec si
.seed:
    mov [wm_clip_tab+WCR_X1], ax
    mov [wm_clip_tab+WCR_Y1], dx
    mov [wm_clip_tab+WCR_X2], cx
    mov [wm_clip_tab+WCR_Y2], si
    mov word [wm_clip_n], 1
    call wm_clip_occl
    jc .no
    cmp word [wm_clip_n], 0
    jne .no
    mov word [wm_clip_n], 0
    stc
    jmp short .out
.no:
    mov word [wm_clip_n], 0
    clc
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_clear - disarm clipping
; in:  nothing
; out: nothing
; clobbers: flags
;
; gfx_unlock calls this, so a background painter never has to. It exists for
; the case of a task that wants to draw unclipped again inside the same lock
; hold - and it is what SPEC.md 11.3 rule 3 points at.
; -----------------------------------------------------------------------------
wm_clip_clear:
    mov word [wm_clip_n], 0
    ret

; -----------------------------------------------------------------------------
; wm_clip_sub - internal: subtract one window's occupied rect from the list
; in:  BX = the occluding window ptr; the list holds at least one rect
; out: CF = 1 the list overflowed (nothing about it is valid afterwards)
; clobbers: flags
;
; The occupied rect is (x, y) to (x+w, y+h) INCLUSIVE - the 1px drop shadow
; wm_draw_win puts at (+1,+1) is the caller's problem too, exactly as
; wm_obscured has always counted it. A fullscreen window has no shadow, but
; it covers the screen anyway and one row past the edge costs nothing:
; vga_rect_setup clips.
; -----------------------------------------------------------------------------
wm_clip_sub:
    push ax
    push si
    push di
    mov ax, [bx+W_X]
    mov [wm_cl_ox1], ax
    add ax, [bx+W_W]
    mov [wm_cl_ox2], ax
    mov ax, [bx+W_Y]
    mov [wm_cl_oy1], ax
    add ax, [bx+W_H]
    mov [wm_cl_oy2], ax
    mov si, wm_clip_tab
    xor di, di                  ; DI = index of the rect under test
.rect:
    cmp di, [wm_clip_n]
    jae .ok
    mov ax, [si+WCR_X2]
    cmp ax, [wm_cl_ox1]
    jl .keep
    mov ax, [si+WCR_X1]
    cmp ax, [wm_cl_ox2]
    jg .keep
    mov ax, [si+WCR_Y2]
    cmp ax, [wm_cl_oy1]
    jl .keep
    mov ax, [si+WCR_Y1]
    cmp ax, [wm_cl_oy2]
    jg .keep
    call wm_clip_split          ; overlap: up to four fragments replace it
    jc .bad
    jmp short .rect             ; slot DI now holds the rect moved down into
                                ; it, so re-test the same index
.keep:
    inc di
    add si, WCR_SZ
    jmp short .rect
.ok:
    clc
    jmp short .out
.bad:
    stc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_split - internal: replace one overlapped rect with its fragments
; in:  SI -> the rect (index DI), wm_cl_o* = the occluder, overlap proven
; out: CF = 1 the list is full
; clobbers: flags
;
; Removal is "move the last rect down into this slot and shorten the list",
; so the order of the list is not preserved - nothing depends on it. The
; fragments are appended at the end, where wm_clip_sub's own walk will reach
; them and find (by construction) that they do not touch the occluder.
; -----------------------------------------------------------------------------
wm_clip_split:
    push ax
    push bx
    mov ax, [si+WCR_X1]         ; the rect is about to be overwritten
    mov [wm_cl_rx1], ax
    mov ax, [si+WCR_Y1]
    mov [wm_cl_ry1], ax
    mov ax, [si+WCR_X2]
    mov [wm_cl_rx2], ax
    mov ax, [si+WCR_Y2]
    mov [wm_cl_ry2], ax

    mov ax, [wm_clip_n]         ; drop it: the last rect moves into its slot
    dec ax                      ; (a self-copy when it WAS the last)
    mov [wm_clip_n], ax
    call wm_clip_ptr            ; BX -> the last rect
    mov ax, [bx+WCR_X1]
    mov [si+WCR_X1], ax
    mov ax, [bx+WCR_Y1]
    mov [si+WCR_Y1], ax
    mov ax, [bx+WCR_X2]
    mov [si+WCR_X2], ax
    mov ax, [bx+WCR_Y2]
    mov [si+WCR_Y2], ax

    mov ax, [wm_cl_rx1]         ; above: full width, r.y1 .. o.y1-1
    mov [wm_cl_fx1], ax
    mov ax, [wm_cl_rx2]
    mov [wm_cl_fx2], ax
    mov ax, [wm_cl_ry1]
    mov [wm_cl_fy1], ax
    mov ax, [wm_cl_oy1]
    dec ax
    mov [wm_cl_fy2], ax
    call wm_clip_add
    jc .full
    mov ax, [wm_cl_oy2]         ; below: full width, o.y2+1 .. r.y2
    inc ax
    mov [wm_cl_fy1], ax
    mov ax, [wm_cl_ry2]
    mov [wm_cl_fy2], ax
    call wm_clip_add
    jc .full

    mov ax, [wm_cl_ry1]         ; the band the occluder shares with us
    cmp ax, [wm_cl_oy1]
    jge .my1
    mov ax, [wm_cl_oy1]
.my1:
    mov [wm_cl_fy1], ax
    mov ax, [wm_cl_ry2]
    cmp ax, [wm_cl_oy2]
    jle .my2
    mov ax, [wm_cl_oy2]
.my2:
    mov [wm_cl_fy2], ax
    mov ax, [wm_cl_rx1]         ; left of it
    mov [wm_cl_fx1], ax
    mov ax, [wm_cl_ox1]
    dec ax
    mov [wm_cl_fx2], ax
    call wm_clip_add
    jc .full
    mov ax, [wm_cl_ox2]         ; and right of it
    inc ax
    mov [wm_cl_fx1], ax
    mov ax, [wm_cl_rx2]
    mov [wm_cl_fx2], ax
    call wm_clip_add
    jc .full
    clc
    jmp short .out
.full:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_add - internal: append wm_cl_f* to the list if it is non-empty
; in:  wm_cl_fx1/fy1/fx2/fy2
; out: CF = 1 the list is full (nothing appended)
; clobbers: flags
; -----------------------------------------------------------------------------
wm_clip_add:
    push ax
    push bx
    mov ax, [wm_cl_fx1]
    cmp ax, [wm_cl_fx2]
    jg .empty
    mov ax, [wm_cl_fy1]
    cmp ax, [wm_cl_fy2]
    jg .empty
    mov ax, [wm_clip_n]
    cmp ax, WM_CLIP_MAX
    jae .full
    inc word [wm_clip_n]
    call wm_clip_ptr            ; BX -> the new slot
    mov ax, [wm_cl_fx1]
    mov [bx+WCR_X1], ax
    mov ax, [wm_cl_fy1]
    mov [bx+WCR_Y1], ax
    mov ax, [wm_cl_fx2]
    mov [bx+WCR_X2], ax
    mov ax, [wm_cl_fy2]
    mov [bx+WCR_Y2], ax
.empty:
    clc
    jmp short .out
.full:
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_clip_ptr - internal: clip-list index to rect pointer
; in:  AX = index
; out: BX = wm_clip_tab + index*WCR_SZ
; clobbers: flags
; -----------------------------------------------------------------------------
wm_clip_ptr:
    push cx
    mov bx, ax
    mov cl, 3                   ; WCR_SZ is 8, and it is meant to stay 8
    shl bx, cl
    add bx, wm_clip_tab
    pop cx
    ret

; -----------------------------------------------------------------------------
; wm_clip_test - may a whole rect be drawn?
; in:  AX = x1, BX = y1, CX = x2, DX = y2 (inclusive)
; out: CF = 0 the rect lies wholly inside one clip fragment, or the clip is
;      disarmed; CF = 1 it does not - the caller must draw NOTHING
; clobbers: flags
;
; This is the answer for the two primitives that cannot draw a partial
; shape: font_char's 8x8 cell and ico_core's icon body (SPEC.md 6/25). Both
; already skip a glyph that would cross a SCREEN edge; a clip edge is the
; same decision one region in.
; -----------------------------------------------------------------------------
wm_clip_test:
    cmp word [wm_clip_n], 0
    je .free
    push si
    push di
    mov si, wm_clip_tab
    mov di, [wm_clip_n]
.frag:
    cmp ax, [si+WCR_X1]
    jl .next
    cmp cx, [si+WCR_X2]
    jg .next
    cmp bx, [si+WCR_Y1]
    jl .next
    cmp dx, [si+WCR_Y2]
    jle .in
.next:
    add si, WCR_SZ
    dec di
    jnz .frag
    stc
    jmp short .out
.in:
    clc
.out:
    pop di
    pop si
    ret
.free:
    clc
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
    mov ah, 0                   ; index * WIN_SIZE, spelled as a real 8-bit
    mov cl, WIN_SIZE            ; multiply rather than a sum of shifts: the
    mul cl                      ; stride has already changed twice (16 -> 18
    mov bx, ax                  ; -> 20, SPEC.md 11) and a shift idiom is a
    add bx, wm_wins             ; silent breakage every time it does
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; wm_draw_title - one window's title bar, in the form its rank calls for
; in:  DI = window ptr, BP = the frontmost visible window (or 0); gfx lock
;      held by the caller
; out: nothing (all registers preserved)
;
; Split out of wm_draw_win because it is the ONLY part of a window that
; changes when some OTHER window is raised: the pinstripes, the close box
; and the minimize box belong to the frontmost window alone (DI == BP), so
; the outgoing front window needs exactly this rect redrawn and nothing
; else. wm_show's fast path (SPEC.md 11.90) is the caller that cares; every
; other path reaches it through wm_draw_win and never notices.
; -----------------------------------------------------------------------------
wm_draw_title:
    test word [di+W_FLAGS], WF_FULL
    jnz .none                   ; a fullscreen window HAS no title bar
                                ; (SPEC.md 11.2), and drawing one anyway put
                                ; a full-width white strip with a centred
                                ; caption across the top of its content. It
                                ; never showed while wm_fast_ok sent every
                                ; fullscreen case to wm_paint_all instead;
                                ; the guard belongs here rather than at each
                                ; caller, because "no title bar" is a fact
                                ; about the window and not about the path
    push ax
    push bx
    push cx
    push dx
    push si
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
    jz .out                     ; no title string: nothing more to draw
    push es                     ; the title is the OWNER's string, so it is
    call wm_strseg              ; read through ES: the package's segment for
    call font_width_x           ; a package window, ours for everything else
    pop es
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
    push es
    call wm_strseg
    call font_str_x
    pop es
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.none:
    ret

; -----------------------------------------------------------------------------
; wm_title_set - change a window's caption and redraw ONLY the caption
; in:  BX = window ptr, AX = the new W_TITLE offset, or 0 to leave W_TITLE
;      alone (the bytes it already points at changed in place); caller HOLDS
;      the gfx lock
; out: nothing (all registers preserved)
;
; The answer to "the title bar was drawn before the window knew what it wanted
; to be called". A caption changes on an EVENT - a folder was entered, a file
; was opened - and never on a paint, so the fix is not a hook that asks every
; window for its name on every frame: it is a call the owner makes at the
; moment it decides, and which draws that one strip and nothing else. Before
; this, the file manager's only way to correct its caption was a flag that
; escalated the next repaint to the whole FRAME, so stepping into a folder
; redrew the window's contents, its listing and everything overlapping it.
;
; The rect is x .. x+w-1, y .. y+TITLE_H-1, which is the strip wm_draw_title
; owns, and nothing outside it is touched: no W_PAINT, no cascade. The region
; arithmetic is wm_covered's (SPEC.md 11.3) seeded on that strip instead of
; the frame, so an occluding window's pixels stay its own.
;
; The granularity rule decides which of three ways it goes. The caption is a
; white gap fill with glyphs on top; a fill clips per pixel and a glyph per
; whole 8x8 cell, so a clip edge across the strip would erase the text and
; not put it back - blank, not stale. So the cheap path is taken only when
; the WHOLE strip is one fragment (nothing over it). Wholly covered draws
; nothing at all. Anything in between - or a region that overflowed - falls
; back to wm_paint_dmg over the strip, which is still 17 rows rather than a
; screen.
; -----------------------------------------------------------------------------
wm_title_set:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    or ax, ax
    jz .keep
    mov [bx+W_TITLE], ax
.keep:
    mov di, bx                  ; DI = the window for the duration; BX stays
                                ; the window until wm_clip_occl has had it
    test word [di+W_FLAGS], 2
    jz .out                     ; hidden: no pixels on screen to correct
    test word [di+W_FLAGS], WF_FULL
    jnz .out                    ; fullscreen has no title bar (SPEC.md 11.2)

    mov word [wm_clip_n], 0
    mov ax, [di+W_X]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, [di+W_Y]
    mov si, dx
    add si, TITLE_H-1
    mov [wm_clip_tab+WCR_X1], ax
    mov [wm_clip_tab+WCR_Y1], dx
    mov [wm_clip_tab+WCR_X2], cx
    mov [wm_clip_tab+WCR_Y2], si
    mov word [wm_clip_n], 1
    call wm_clip_occl           ; subtract every visible window above us
    jc .dmg                     ; overflowed: nothing about the list is valid
    cmp word [wm_clip_n], 0
    je .clear                   ; wholly covered: not one pixel of it is ours

    mov ax, [di+W_X]            ; is the whole strip one fragment? wm_clip_test
    mov cx, ax                  ; reads an empty list as "disarmed, draw
    add cx, [di+W_W]            ; freely", which is why the covered case had
    dec cx                      ; to be answered above it and not here
    mov bx, [di+W_Y]
    mov dx, bx
    add dx, TITLE_H-1
    call wm_clip_test
    jc .dmg
    call wm_clip_clear          ; nothing is over it: the clip has no work to
    call wm_top                 ; do, and wm_draw_title stays inside the strip
    mov bp, bx                  ; BP = the frontmost visible window (or 0):
    mov bx, di                  ; the pinstripes and the two boxes are its
    call wm_draw_title          ; alone (SPEC.md 11.90)
    jmp short .out
.clear:
    call wm_clip_clear
    jmp short .out
.dmg:
    call wm_clip_clear
    mov ax, [di+W_X]
    mov bx, [di+W_Y]
    mov cx, ax
    add cx, [di+W_W]
    dec cx
    mov dx, bx
    add dx, TITLE_H-1
    call wm_paint_dmg
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

    call wm_draw_title          ; the title bar, active or not (BP decides)

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
    push bp
    mov bp, [bx+W_PAINT]
    call wm_pkgcall             ; near for a kernel window, far into the
    pop bp                      ; package's dispatcher otherwise (SPEC 20.1)
    mov bx, si                  ; (wm_pkgcall preserves both, but the paint
                                ; proc need not preserve BX)
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
wm_wins:    resb MAX_WIN * WIN_SIZE ; 12 windows x WIN_SIZE bytes
wm_zord:    resb MAX_WIN            ; window indices, [0] = backmost
wm_zn:      resb 1                  ; entries in wm_zord
wm_about:   resw MAX_WIN            ; per-slot About handler offset, 0 = none
                                    ; (SPEC.md 12.2); inside wm_init's zero
                                    ; run, so a fresh slot has no About
wm_owner:   resb MAX_WIN            ; instance index owning each window slot
                                    ; (SPEC.md 29), 0xFF = unowned; NOT part
                                    ; of wm_init's zero run (fill is 0xFF)
wm_fs:      resw 1                  ; fullscreen window ptr, 0 = none
                                    ; (SPEC.md 11.2); zeroed by wm_init
wm_fs_save: resw 4                  ; its windowed x/y/w/h, restored on exit

; --- the clip region (SPEC.md 11.3) ------------------------------------------
; Uninitialized on purpose: wm_clip_n lives in .text as a dw 0 and is the
; only thing that says how much of this is meaningful, so nothing here needs
; a value before wm_clip_set writes one.
wm_clip_tab: resw 4 * WM_CLIP_MAX   ; 16 rects x {x1, y1, x2, y2}, inclusive
wm_cl_ox1:  resw 1                  ; wm_clip_sub: the occluder's rect, its
wm_cl_oy1:  resw 1                  ; drop shadow included
wm_cl_ox2:  resw 1
wm_cl_oy2:  resw 1
wm_cl_rx1:  resw 1                  ; wm_clip_split: the rect being replaced
wm_cl_ry1:  resw 1
wm_cl_rx2:  resw 1
wm_cl_ry2:  resw 1
wm_cl_fx1:  resw 1                  ; wm_clip_add: the fragment to append
wm_cl_fy1:  resw 1
wm_cl_fx2:  resw 1
wm_cl_fy2:  resw 1
section .text
