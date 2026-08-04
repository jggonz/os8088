; =============================================================================
; os8088 - dock.inc
;
; The dock strip (SPEC.md 30): a taskbar along the bottom of the screen with
; one tile per RUNNING instance (I_STATE = 1, SPEC.md 29), built-in or
; package. Tile position i belongs to instance-table record i - a stable
; mapping with holes, so quitting one instance never moves another's tile.
;
; A tile carries TWO independent states, and they are drawn as two different
; kinds of mark on purpose. Minimized (I_FLAGS bit0) inverts the tile's
; interior - the app is not on screen at all. ACTIVE - the instance that owns
; the frontmost visible window - doubles the tile's border instead. They are
; mutually exclusive in practice (a minimized window is not visible, so it
; cannot be frontmost) but nothing depends on that, and a heavier border
; survives the 1bpp reduction (SPEC.md 39.4) where a second colour would not.
; Clicking a tile restores a minimized instance or fronts a visible one.
;
; dock_paint runs inside wm_paint_all right after desk_paint, under the
; caller's gfx lock - windows paint over the dock exactly like they cover
; the desktop icons (SPEC.md 26/30). dock_click is called by the UI task
; with no lock held, only after wm_hit declined the click, and takes the
; lock itself around wm calls.
;
; **It is called on every window operation and usually has nothing to do.**
; Raising a window, showing one, dragging one - each of those repaints the
; chrome because the chrome MIGHT have changed, and most of the time the only
; thing that did is which tile wears the active mark. So each tile carries a
; key (dock_key: its icon, mixed with live/minimized/active) and a tile whose
; key still matches what was drawn is left alone. A focus change costs two
; tiles; nothing changing costs no pixels at all, and dock_paint says so in
; CF, which is what lets wm_dock_under skip putting the windows back.
;
; The strip's own rule and white field are only redrawn when [dock_full] says
; so, and the one thing that sets it is somebody having drawn OVER the strip:
; wm_paint_all's dither, and wm_paint_dmg's when the damage reaches it - which
; is how "a window that was covering the dock moved away" gets its full
; redraw. The per-tile keys are cleared with it, because the tiles went too.
; =============================================================================

; --- geometry (SPEC.md 30) ----------------------------------------------------
DOCK_H      equ 24              ; strip height: 1px black rule + 23 white
                                ; rows, pinned to the BOTTOM of whatever
                                ; screen the adapter gave us. The top row and
                                ; the tile top row therefore move with it and
                                ; live in [vid_dock_y0] / [vid_dock_ty0]
                                ; (SPEC.md 39.2) - 456/459 on VGA, 324/327 on
                                ; Hercules, 176/179 on CGA.
DOCK_TILE_W equ 24
DOCK_TILE_H equ 20
DOCK_X0     equ 8               ; first tile's left edge
DOCK_STEP   equ 28              ; tile + 4px gap; 8 + 12*28 = 344 < 640

; -----------------------------------------------------------------------------
; dock_init - reset module scratch
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
dock_init:
    push cx
    push di
    mov byte [dock_gcol], 0
    mov byte [dock_full], 1     ; .bss is not zeroed at boot (-f bin), and
    mov di, dock_ck             ; the first paint owes the whole strip
    mov cx, INST_MAX
.z:
    mov word [di], 0
    inc di
    inc di
    loop .z
    pop di
    pop cx
    ret

; -----------------------------------------------------------------------------
; dock_force - the whole strip must be redrawn, tiles and all
; in:  nothing
; out: nothing (all registers preserved)
;
; Called by whoever is about to draw, or has just drawn, over the strip. The
; per-tile keys go with it: a tile whose pixels were painted over is not
; "unchanged" however much its state still matches.
; -----------------------------------------------------------------------------
dock_force:
    mov byte [dock_full], 1
    ret

; -----------------------------------------------------------------------------
; dock_paint - bring the strip up to date: the rule and white field when
;              [dock_full] says they are owed, then every tile whose key moved
; in:  nothing (caller holds the gfx lock)
; out: CF = 1 if any pixel was drawn, CF = 0 if the strip was already right.
;      All registers preserved; [gfx_color] restored.
; -----------------------------------------------------------------------------
dock_paint:
    push ax
    push bx
    push cx
    push dx
    push di

    mov al, [gfx_color]         ; byte-only save: gfx_lock_flag lives next
    mov [dock_gcol], al         ; door (see desk_draw_zone's precedent)
    mov byte [dock_drew], 0

    call wm_top                 ; who owns the frontmost visible window? Its
    mov word [dock_act], 0      ; tile gets the active mark (0 = nobody), and
    or bx, bx                   ; every tile's key depends on it, so it is
    jz .full                    ; resolved before the first one is tested
    call inst_win_owner         ; DI = its instance record, or 0
    mov [dock_act], di

.full:
    cmp byte [dock_full], 0     ; somebody drew over us: rule, field and every
    je .scan                    ; tile, and the keys are worthless
    mov byte [dock_full], 0
    mov byte [dock_drew], 1

    mov byte [gfx_color], CBLACK
    xor ax, ax                  ; the rule: full-width hline at the top of
    mov bx, [vid_wm1]           ; the strip
    mov dx, [vid_dock_y0]
    call gfx_hline

    mov byte [gfx_color], CWHITE
    xor ax, ax                  ; the strip: white, down to the last row
    mov bx, [vid_dock_y0]
    inc bx
    mov cx, [vid_wm1]
    mov dx, [vid_hm1]
    call gfx_fill

    mov di, dock_ck             ; ...and nothing on it survived
    mov cx, INST_MAX
.zap:
    mov word [di], 0
    inc di
    inc di
    loop .zap

.scan:
    mov di, inst_tab            ; one tile per live record, stable slots
    xor cx, cx                  ; CX = instance index
.tile:
    call dock_key               ; AX = what this tile should look like, or 0
    mov bx, cx                  ; for no tile at all (SPEC.md 29.2 rule 3)
    shl bx, 1
    cmp ax, [dock_ck+bx]
    je .next                    ; already drawn that way
    mov [dock_ck+bx], ax
    mov byte [dock_drew], 1
    push ax
    call dock_erase_tile        ; the old tile's marks are not a subset of the
    pop ax                      ; new one's, so start from white either way
    or ax, ax
    jz .next                    ; the tile went away: erased is all it needed
    call dock_draw_tile         ; in: CX = index, DI = record
.next:
    add di, I_RECSZ
    inc cx
    cmp cx, INST_MAX
    jb .tile

    mov al, [dock_gcol]
    mov [gfx_color], al

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [dock_drew], 0     ; POP does not touch flags, but say it here
    je .clean                   ; anyway rather than depend on that
    stc
    ret
.clean:
    clc
    ret

; -----------------------------------------------------------------------------
; dock_key - what tile i should look like, in one word
; in:  CX = instance index, DI = its record
; out: AX = the key, 0 = this slot has no tile
; clobbers: nothing else (flags)
;
; Everything dock_draw_tile reads: the icon it blits, and the three bits that
; decide the marks around it. The icon pointer is rotated rather than added so
; that its high bits reach the bits the marks use - two different apps in the
; same slot must not key alike, and a slot can be freed and refilled between
; two paints.
; -----------------------------------------------------------------------------
dock_key:
    push cx
    xor ax, ax
    cmp byte [di+I_STATE], 1    ; live only - dying and free records have no
    jne .out                    ; tile (SPEC.md 29.2 rule 3)
    mov ax, [di+I_ICON]
    mov cl, 3
    rol ax, cl
    xor al, 1                   ; live
    test byte [di+I_FLAGS], 1
    jz .nomini
    xor al, 2                   ; minimized: the interior inverts
.nomini:
    cmp di, [dock_act]
    jne .noact
    xor al, 4                   ; active: the border doubles
.noact:
    or ax, ax                   ; 0 is spoken for
    jnz .out
    inc ax
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; dock_tile_x - tile i's left edge
; in:  CX = instance index
; out: AX = x
; clobbers: BX (flags)
; -----------------------------------------------------------------------------
dock_tile_x:
    mov ax, cx                  ; tile left = DOCK_X0 + index*DOCK_STEP
    mov bl, DOCK_STEP           ; (28 is not a power of two: multiply)
    mul bl                      ; AX = AL*BL - index <= 11, fits AL
    add ax, DOCK_X0
    ret

; -----------------------------------------------------------------------------
; dock_erase_tile - white out tile i's rect
; in:  CX = instance index; gfx lock held by the caller
; out: nothing (all registers preserved)
;
; A tile is redrawn from white because its marks are not nested: going from
; active to plain has to remove the inner frame, and going from minimized to
; plain has to un-invert an interior that a second XOR would only re-invert
; if the tile had not also been redrawn underneath it.
; -----------------------------------------------------------------------------
dock_erase_tile:
    push ax
    push bx
    push cx
    push dx
    call dock_tile_x            ; AX = tile left
    mov byte [gfx_color], CWHITE
    mov cx, ax
    add cx, DOCK_TILE_W-1
    mov bx, [vid_dock_ty0]
    mov dx, bx
    add dx, DOCK_TILE_H-1
    call gfx_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_draw_tile - one tile: frame + icon (+ XOR invert when minimized)
; in:  CX = instance index, DI = instance record (I_STATE = 1); gfx lock
;      held by the caller
; out: nothing (all registers preserved; module internal)
; -----------------------------------------------------------------------------
dock_draw_tile:
    push ax
    push bx
    push cx
    push dx
    push si

    call dock_tile_x
    mov si, ax                  ; SI = tile left for the duration

    mov byte [gfx_color], CBLACK
    mov ax, si                  ; 1px tile frame
    mov bx, [vid_dock_ty0]
    mov cx, si
    add cx, DOCK_TILE_W-1
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-1
    call gfx_frame

    mov cx, si                  ; icon at (left+4, top+2): 16x16 body
    add cx, 4
    mov dx, [vid_dock_ty0]
    add dx, 2
    push si
    mov si, [di+I_ICON]
    or si, si
    jnz .icon
    mov si, ico_app16 + 2       ; generic fallback: skip the library
.icon:                          ; record's 2-byte header (SPEC.md 25/30)
    call icon_draw16
    pop si

    cmp di, [dock_act]          ; active: a second frame just inside the
    jne .mini                   ; first, which reads as a heavier border and
    mov byte [gfx_color], CBLACK; costs no colour (SPEC.md 39.4). The icon
    mov ax, si                  ; body is at +4,+2 and 16x16, so it clears
    inc ax                      ; both new edges by 2px and 1px
    mov bx, [vid_dock_ty0]
    inc bx
    mov cx, si
    add cx, DOCK_TILE_W-2
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-2
    call gfx_frame

.mini:
    test byte [di+I_FLAGS], 1   ; minimized: invert the tile interior
    jz .done
    mov ax, si
    inc ax
    mov bx, [vid_dock_ty0]
    inc bx
    mov cx, si
    add cx, DOCK_TILE_W-2
    mov dx, [vid_dock_ty0]
    add dx, DOCK_TILE_H-2
    call gfx_xor_fill

.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dock_click - hit-test a click against the strip (SPEC.md 30)
; in:  CX = x, DX = y (absolute); NO lock held (takes it internally)
; out: CF = 1 the click was consumed (anywhere in the strip, tile or not),
;      CF = 0 not in the dock. All registers preserved.
;
; Tile hit on a live instance: minimized -> inst_restore (clears the flag,
; wm_show); visible -> wm_front on its window. Single click activates.
; -----------------------------------------------------------------------------
dock_click:
    push ax
    push bx
    push cx
    push di

    cmp dx, [vid_dock_y0]       ; below the rule?
    jl .miss

    cmp cx, DOCK_X0             ; left of the first tile: consumed no-op
    jl .consumed
    sub cx, DOCK_X0
    mov ax, cx
    mov bl, DOCK_STEP           ; slot = (x - DOCK_X0) / DOCK_STEP
    div bl                      ; AL = slot, AH = within-slot x
    cmp ah, DOCK_TILE_W         ; in the gap between tiles?
    jae .consumed
    cmp al, INST_MAX            ; past the last slot?
    jae .consumed

    call inst_ptr               ; DI = record (in: AL = index)
    cmp byte [di+I_STATE], 1    ; empty/dying slot: consumed no-op
    jne .consumed

    test byte [di+I_FLAGS], 1
    jz .front
    call gfx_lock               ; minimized: restore (clears bit0, wm_show)
    call inst_restore           ; in: DI = record; lock held
    call gfx_unlock
    jmp .consumed

.front:
    call gfx_lock               ; visible: bring to front (idempotent)
    mov bx, [di+I_WIN]
    call wm_front
    call gfx_unlock

.consumed:
    stc
    jmp .out
.miss:
    clc
.out:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

dock_gcol:  resb 1              ; dock_paint's [gfx_color] save
dock_full:  resb 1              ; the rule and the white field are owed
dock_drew:  resb 1              ; ...and dock_paint's CF: did anything move?
dock_act:   resw 1              ; the instance record owning the frontmost
                                ; visible window, 0 = none. Read per tile, so
                                ; it is resolved once per dock_paint
dock_ck:    resw INST_MAX       ; what each tile was last DRAWN as (dock_key)

section .text
