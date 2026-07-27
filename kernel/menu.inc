; =============================================================================
; os8088 - menu.inc
;
; The menu bar and pull-down menus (SPEC.md 12). Three menus: the System
; menu (an 11x11 hand-authored chip silhouette - the os8088 logo, and the
; one place bitmap data is hand-made), File, and Special. menu_track runs the entire mouse
; interaction while the button is held: the caller (the UI task) already
; holds the gfx lock, so tracking reads mouse_x/mouse_y/mouse_btn directly
; (the ISR keeps them fresh; the cursor stays hidden - the highlight is
; the feedback). The pixels under the dropped menu are saved to SAVE_SEG:0
; and put back on release.
; =============================================================================

; --- commands returned by menu_track (owned here, dispatched by ui.inc) ------
CMD_ABOUT   equ 1
CMD_NOTE    equ 2
CMD_CLOCK   equ 3
CMD_BOUNCE  equ 4
CMD_FILES   equ 5
CMD_CLOSE   equ 6
CMD_TASKS   equ 7
CMD_REBOOT  equ 8

; --- menu table entry layout --------------------------------------------------
MENU_TITLE  equ 0               ; word: near ptr to NUL title, 0 = logo glyph
MENU_ITEMS  equ 2               ; word: near ptr to array of item string ptrs
MENU_NITEM  equ 4               ; word: item count
MENU_BASE   equ 6               ; word: CMD_* of item 0 (items are consecutive)
MENU_XL     equ 8               ; word: bar hit range, left x (inclusive)
MENU_XR     equ 10              ; word: bar hit range, right x (inclusive)
MENU_TX     equ 12              ; word: title text / glyph left x
MENU_ENTSZ  equ 14
MENU_NUM    equ 3               ; menus in the table

MENU_TEXT_Y equ 6               ; title text row (8px glyphs in a 19px bar)
MENU_LOGO_Y equ 4               ; logo glyph top row (11px tall)
MENU_ITEM_H equ 16              ; pull-down item cell height, px

; -----------------------------------------------------------------------------
; menu_draw_bar - draw the menu bar: white rows 0..MBAR_H-2, black line at
;                 row MBAR_H-1, logo glyph + titles at their fixed x.
; in:  nothing (caller holds the gfx lock)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_draw_bar:
    push ax
    push bx
    push cx
    push dx
    push si

    mov byte [gfx_color], CWHITE
    xor ax, ax                  ; white field, rows 0..MBAR_H-2
    xor bx, bx
    mov cx, SCREEN_W - 1
    mov dx, MBAR_H - 2
    call gfx_fill

    mov byte [gfx_color], CBLACK
    xor ax, ax                  ; the black rule along the bottom
    mov bx, SCREEN_W - 1
    mov dx, MBAR_H - 1
    call gfx_hline

    mov si, menu_table          ; titles (gfx_color is already black)
    mov cx, MENU_NUM
.title:
    push cx
    mov ax, [si + MENU_TITLE]
    test ax, ax
    jz .glyph
    mov cx, [si + MENU_TX]
    mov dx, MENU_TEXT_Y
    push si
    mov si, ax
    call font_str
    pop si
    jmp .next
.glyph:
    mov cx, [si + MENU_TX]
    call menu_logo_glyph
.next:
    pop cx
    add si, MENU_ENTSZ
    loop .title

    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_track - run one full menu interaction. Finds the title under the
;              mousedown, xor-highlights it, drops the menu (save-under to
;              SAVE_SEG:0), follows the mouse with an item highlight, and on
;              release restores the screen and un-highlights the title.
;              Polls mouse_btn/mouse_x/mouse_y directly and yields every
;              iteration so the scheduler stays healthy (background tasks
;              block on the gfx lock, which we hold, until we return).
; in:  CX = mousedown x (caller holds the gfx lock)
; out: AX = CMD_* of the selected item, 0 if none
; clobbers: nothing else
; -----------------------------------------------------------------------------
menu_track:
    push bx
    push cx
    push dx
    push si
    push di
    push es

    ; --- which title was hit? ------------------------------------------------
    mov si, menu_table
    mov di, MENU_NUM
.find:
    cmp cx, [si + MENU_XL]
    jb .miss
    cmp cx, [si + MENU_XR]
    jbe .found
.miss:
    add si, MENU_ENTSZ
    dec di
    jnz .find
    xor ax, ax                  ; dead zone between titles: no menu, no work
    jmp .out

.found:
    mov [menu_ent], si
    call menu_title_xor         ; highlight the title cell

    ; --- menu rect: width = widest item + 16, items are 16px cells -----------
    call menu_widest            ; AX = widest item text, px (SI = entry)
    add ax, 16
    mov bx, [si + MENU_XL]
    mov [menu_x1], bx
    add bx, ax
    dec bx
    mov [menu_x2], bx           ; x2 = x1 + width - 1
    mov ax, [si + MENU_NITEM]
    mov [menu_cnt], ax
    mov cl, 4
    shl ax, cl                  ; count * 16
    add ax, MBAR_H + 1          ; + top frame row; bottom frame is y2 itself
    mov [menu_y2], ax
    mov ax, [si + MENU_BASE]
    mov [menu_base], ax

    ; --- save the pixels under the menu to SAVE_SEG:0 -------------------------
    mov ax, SAVE_SEG
    mov es, ax
    xor di, di
    mov ax, [menu_x1]
    mov bx, MBAR_H
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_save

    ; --- draw the menu: white fill, black frame, items 8px in from the left --
    mov byte [gfx_color], CWHITE
    mov ax, [menu_x1]
    mov bx, MBAR_H
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_fill
    mov byte [gfx_color], CBLACK
    mov ax, [menu_x1]
    mov bx, MBAR_H
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_frame

    mov si, [menu_ent]
    mov bx, [si + MENU_ITEMS]
    mov cx, [si + MENU_NITEM]
    mov dx, MBAR_H + 1 + 4      ; first cell top + 4px pad centers 8px glyphs
.item:
    push cx
    mov si, [bx]
    mov cx, [menu_x1]
    add cx, 8
    call font_str
    pop cx
    add bx, 2
    add dx, MENU_ITEM_H
    loop .item

    ; --- follow the mouse while the button is down ----------------------------
    mov word [menu_sel], 0xFFFF ; nothing highlighted yet
.poll:
    test byte [mouse_btn], 1
    jz .up
    call menu_hover             ; AX = hovered item, 0xFFFF if outside
    cmp ax, [menu_sel]
    je .same
    push ax                     ; highlight moved: erase old, draw new
    mov ax, [menu_sel]
    cmp ax, 0xFFFF
    je .no_old
    call menu_item_xor
.no_old:
    pop ax
    mov [menu_sel], ax
    cmp ax, 0xFFFF
    je .same
    call menu_item_xor
.same:
    call task_yield             ; keep the scheduler healthy while we spin
    jmp .poll

    ; --- released: put the screen back, report the selection -------------------
.up:
    mov ax, SAVE_SEG            ; restore erases the menu and any highlight
    mov es, ax
    xor si, si
    mov ax, [menu_x1]
    mov bx, MBAR_H
    mov cx, [menu_x2]
    mov dx, [menu_y2]
    call gfx_restore

    call menu_title_xor         ; un-highlight the title (self-inverting)

    mov ax, [menu_sel]
    cmp ax, 0xFFFF
    je .none
    add ax, [menu_base]
    jmp .out
.none:
    xor ax, ax

.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_hover - which item cell is the mouse inside right now?
; in:  [menu_x1]/[menu_x2]/[menu_cnt] set up by menu_track
; out: AX = item index, 0xFFFF if the mouse is outside the menu rect
; clobbers: nothing else
; -----------------------------------------------------------------------------
menu_hover:
    push cx
    mov ax, [mouse_x]
    cmp ax, [menu_x1]
    jb .none
    cmp ax, [menu_x2]
    ja .none
    mov ax, [mouse_y]
    sub ax, MBAR_H + 1          ; rows above the first cell borrow out
    jb .none
    mov cl, 4
    shr ax, cl                  ; / 16 -> cell index
    cmp ax, [menu_cnt]
    jae .none                   ; below the last cell (or on the frame)
    pop cx
    ret
.none:
    mov ax, 0xFFFF
    pop cx
    ret

; -----------------------------------------------------------------------------
; menu_item_xor - toggle the highlight on one item cell (xor is its own undo)
; in:  AX = item index; [menu_x1]/[menu_x2] set up by menu_track
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_item_xor:
    push ax
    push bx
    push cx
    push dx
    mov cl, 4
    shl ax, cl                  ; index * 16
    add ax, MBAR_H + 1          ; cell top row
    mov bx, ax
    mov dx, ax
    add dx, MENU_ITEM_H - 1     ; cell bottom row
    mov ax, [menu_x1]
    inc ax                      ; keep the frame out of the highlight
    mov cx, [menu_x2]
    dec cx
    call gfx_xor_fill
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_title_xor - toggle the highlight on the tracked menu's title cell
; in:  [menu_ent] -> menu table entry
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_title_xor:
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, [menu_ent]
    mov ax, [si + MENU_XL]
    xor bx, bx
    mov cx, [si + MENU_XR]
    mov dx, MBAR_H - 2          ; the black rule stays black
    call gfx_xor_fill
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu_widest - widest item text in a menu, in pixels
; in:  SI -> menu table entry
; out: AX = max font_width over the menu's items
; clobbers: nothing else
; -----------------------------------------------------------------------------
menu_widest:
    push bx
    push cx
    push dx
    push si
    mov bx, [si + MENU_ITEMS]
    mov cx, [si + MENU_NITEM]
    xor dx, dx                  ; running max
.next:
    mov si, [bx]
    call font_width             ; AX = 8 * strlen
    cmp ax, dx
    jbe .keep
    mov dx, ax
.keep:
    add bx, 2
    loop .next
    mov ax, dx
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; menu_logo_glyph - draw the 11x11 chip silhouette (the os8088 logo) in
; [gfx_color]
; in:  CX = left x (top is MENU_LOGO_Y)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
menu_logo_glyph:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov si, menu_logo_bmp
    mov di, cx                  ; DI = left x
    mov dx, MENU_LOGO_Y
    mov bh, 11                  ; rows
.row:
    lodsw                       ; bit 15 = leftmost pixel of the row
    mov cx, di
    mov bl, 11                  ; columns
.col:
    shl ax, 1
    jnc .skip
    call gfx_pixel              ; CX = x, DX = y
.skip:
    inc cx
    dec bl
    jnz .col
    inc dx
    dec bh
    jnz .row
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; menu data: logo bitmap, strings, item lists, the menu table
; -----------------------------------------------------------------------------

; 11x11 DIP-chip silhouette - the os8088 logo: a 7px-wide package body with
; a notch at the top and four pins down each side. One word per row,
; bit 15 = x offset 0.
menu_logo_bmp:
    dw 0x0000                   ; ...........
    dw 0x3B80                   ; ..###.###..   body top, orientation notch
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..
    dw 0xFFE0                   ; ###########   pins
    dw 0x3F80                   ; ..#######..   body bottom
    dw 0x0000                   ; ...........

menu_s_file     db 'File', 0
menu_s_special  db 'Special', 0

menu_s_about    db 'About os8088...', 0
menu_s_note     db 'Note Pad', 0
menu_s_clock    db 'Clock', 0
menu_s_bounce   db 'Bounce', 0
menu_s_disk     db 'Disk', 0
menu_s_close    db 'Close Window', 0
menu_s_tasks    db 'Task Manager', 0
menu_s_restart  db 'Restart', 0

menu_items_logo:
    dw menu_s_about
menu_items_file:
    dw menu_s_note
    dw menu_s_clock
    dw menu_s_bounce
    dw menu_s_disk
    dw menu_s_close
menu_items_special:
    dw menu_s_tasks
    dw menu_s_restart

; per menu: title (0 = logo glyph), items, count, cmd base,
;           bar hit range [xl..xr], title x. The hit ranges tile the bar's
;           left edge with 12px padding zones around each title.
menu_table:
    dw 0,              menu_items_logo,    1, CMD_ABOUT,  0,   29,  10
    dw menu_s_file,    menu_items_file,    5, CMD_NOTE,   30,  85,  42
    dw menu_s_special, menu_items_special, 2, CMD_TASKS,  86, 165,  98

; -----------------------------------------------------------------------------
section .bss

menu_ent:   resw 1              ; -> menu table entry being tracked
menu_x1:    resw 1              ; dropped menu rect, left
menu_x2:    resw 1              ; dropped menu rect, right
menu_y2:    resw 1              ; dropped menu rect, bottom (top is MBAR_H)
menu_cnt:   resw 1              ; item count of the tracked menu
menu_base:  resw 1              ; CMD_* base of the tracked menu
menu_sel:   resw 1              ; highlighted item, 0xFFFF = none

section .text
