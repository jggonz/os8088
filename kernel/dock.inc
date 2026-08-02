; =============================================================================
; os8088 - dock.inc
;
; The dock strip (SPEC.md 30): a taskbar along the bottom of the screen with
; one tile per RUNNING instance (I_STATE = 1, SPEC.md 29), built-in or
; package. Tile position i belongs to instance-table record i - a stable
; mapping with holes, so quitting one instance never moves another's tile.
; Minimized instances (I_FLAGS bit0) draw inverted; clicking a tile restores
; a minimized instance or fronts a visible one.
;
; dock_paint runs inside wm_paint_all right after desk_paint, under the
; caller's gfx lock - windows paint over the dock exactly like they cover
; the desktop icons (SPEC.md 26/30). dock_click is called by the UI task
; with no lock held, only after wm_hit declined the click, and takes the
; lock itself around wm calls. Every dock-state transition rides a
; wm_show/wm_hide/wm_destroy full repaint, so no partial-redraw path exists.
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
    mov byte [dock_gcol], 0
    ret

; -----------------------------------------------------------------------------
; dock_paint - draw the rule, the white strip, and every live tile
; in:  nothing (caller holds the gfx lock)
; out: nothing (all registers preserved; [gfx_color] restored)
; -----------------------------------------------------------------------------
dock_paint:
    push ax
    push bx
    push cx
    push dx
    push di

    mov al, [gfx_color]         ; byte-only save: gfx_lock_flag lives next
    mov [dock_gcol], al         ; door (see desk_draw_zone's precedent)

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

    mov di, inst_tab            ; one tile per live record, stable slots
    xor cx, cx                  ; CX = instance index
.tile:
    cmp byte [di+I_STATE], 1    ; live only - dying/free records have no
    jne .next                   ; tile (SPEC.md 29.2 rule 3)
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

    mov ax, cx                  ; tile left = DOCK_X0 + index*DOCK_STEP
    mov bl, DOCK_STEP           ; (28 is not a power of two: multiply)
    mul bl                      ; AX = AL*BL - index <= 11, fits AL
    add ax, DOCK_X0
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

section .text
