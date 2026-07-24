; =============================================================================
; jop - apps/mines/mines.asm
;
; Minesweeper, the first software package (SPEC.md 23). Not kernel code: a
; flat .jop binary at org 0xA000 that reaches every kernel service through
; the jop API jump table (jopapi.inc). 9x9 board, 10 mines, 16px cells,
; a 20px status strip above the board. Mines are placed lazily on the first
; reveal (never under it); zero-count reveals flood-fill through an explicit
; queue (no recursion - the UI task's stack is 1536 bytes). 'F' toggles flag
; mode, 'N' starts a new game.
;
; The entry point only creates the window (wm_create is lock-free) and
; returns BX = window ptr / CF clear; the loader shows it. Everything else
; runs inside W_PAINT / W_ONKEY / W_ONCLICK, which the kernel calls with the
; gfx lock already held - so the handlers draw directly and never lock,
; block or spawn. Every handler fetches the content origin via wm_content
; on entry (the window moves). All state lives in loader-zeroed bss after
; the image; the all-zeroes state is exactly a fresh game.
; =============================================================================

%include "jopapi.inc"

    JOP_HEADER 'MINES', mn_entry, 1

; --- embedded 16x16 icon (SPEC.md 20.2, flags bit 0) ---------------------------
; A mine: black disc (rows 4..11) with 8 spokes (N/S/E/W 2px + 1px
; diagonals) and a 2x2 white glint punched out of the disc's upper-left.
; The mask is the disc+spokes silhouette dilated 1px (8-connected) so the
; glyph sits on a clean white underlay over any background.
;
;   data (o = glint, white)      mask
;   ................             ......####......
;   .......##.......             .###..####..###.
;   ..#....##....#..             .####.####.####.
;   ...#...##...#...             .##############.
;   ....#.####.#....             ..############..
;   .....######.....             ...##########...
;   ....#oo#####....             ################
;   .####oo########.             ################
;   .##############.             ################
;   ....########....             ################
;   .....######.....             ...##########...
;   ....#.####.#....             ..############..
;   ...#...##...#...             .##############.
;   ..#....##....#..             .####.####.####.
;   .......##.......             .###..####..###.
;   ................             ......####......
    JOP_ICON16
    dw 0x03C0                       ; 16 mask rows (white underlay)
    dw 0x73CE
    dw 0x7BDE
    dw 0x7FFE
    dw 0x3FFC
    dw 0x1FF8
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0xFFFF
    dw 0x1FF8
    dw 0x3FFC
    dw 0x7FFE
    dw 0x7BDE
    dw 0x73CE
    dw 0x03C0
    dw 0x0000                       ; 16 data rows (black pixels)
    dw 0x0180
    dw 0x2184
    dw 0x1188
    dw 0x0BD0
    dw 0x07E0
    dw 0x09F0
    dw 0x79FE
    dw 0x7FFE
    dw 0x0FF0
    dw 0x07E0
    dw 0x0BD0
    dw 0x1188
    dw 0x2184
    dw 0x0180
    dw 0x0000
    JOP_ICON16_END

; --- board geometry ------------------------------------------------------------
MN_COLS      equ 9                  ; cells per row / column
MN_CELLS     equ 81                 ; 9 x 9
MN_MINES     equ 10
MN_CELLPX    equ 16                 ; cell size, px
MN_STRIP_H   equ 20                 ; status strip height (content rows 0..19)
MN_BOARD_W   equ 144                ; 9 * 16 = full content width
MN_SAFE      equ MN_CELLS - MN_MINES    ; 71 revealed cells = win

; game modes ([mn_mode])
MN_M_FRESH   equ 0                  ; playing, mines not placed yet
MN_M_LIVE    equ 1                  ; playing, mines placed
MN_M_LOST    equ 2
MN_M_WON     equ 3

; per-cell states ([mn_state + cell])
MN_S_COVER   equ 0
MN_S_FLAG    equ 1
MN_S_OPEN    equ 2

MN_BSS_TOTAL equ 426                ; see the bss layout after JOP_IMAGE_END

; -----------------------------------------------------------------------------
; mn_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here.
; The loader has already zeroed our bss, which is a fresh game.
; -----------------------------------------------------------------------------
mn_entry:
    push si
    mov si, mn_tpl
    call JAPI_WM_CREATE             ; BX = window ptr, CF on table full
    pop si
    ret

; -----------------------------------------------------------------------------
; mn_paint - W_PAINT: full content repaint
; in:  SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_paint:
    push ax
    push bx
    push dx
    mov bx, si
    call JAPI_WM_CONTENT            ; AX = content left, DX = content top
    mov [mn_ox], ax
    mov [mn_oy], dx
    call mn_draw_status
    call mn_draw_board
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mn_onkey - W_ONKEY: 'f'/'F' toggles flag mode, 'n'/'N' starts a new game
; in:  AL = ascii, AH = scan, SI = window ptr; caller holds the gfx lock
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cl, al                      ; keep the key; wm_content returns AX/DX
    mov bx, si
    call JAPI_WM_CONTENT
    mov [mn_ox], ax
    mov [mn_oy], dx
    cmp cl, 'f'
    je .flag
    cmp cl, 'F'
    je .flag
    cmp cl, 'n'
    je .new
    cmp cl, 'N'
    je .new
    jmp .out

.flag:                              ; toggle flag mode, refresh the strip text
    xor byte [mn_flagmode], 1
    call mn_draw_status
    jmp .out

.new:                               ; wipe mine/state/adj (contiguous in bss)
    mov bx, 0
.zero:
    mov byte [mn_mine+bx], 0
    inc bx
    cmp bx, MN_CELLS * 3
    jb .zero
    mov byte [mn_flags], 0
    mov byte [mn_revealed], 0
    mov byte [mn_mode], MN_M_FRESH  ; mines re-placed on the next reveal
    call mn_draw_status
    call mn_draw_board

.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mn_onclick - W_ONCLICK: reveal (or flag, in flag mode) the cell hit
; in:  CX = x, DX = y (absolute screen), SI = window ptr; gfx lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [mn_mode], MN_M_LOST
    jae .out                        ; board is dead after win/lose until 'N'
    mov bx, si
    push cx
    push dx
    call JAPI_WM_CONTENT            ; AX = content left, DX = content top
    mov [mn_ox], ax
    mov [mn_oy], dx
    pop dx
    pop cx
    sub cx, [mn_ox]                 ; -> content-relative click
    sub dx, [mn_oy]
    sub dx, MN_STRIP_H
    js .out                         ; click landed in the status strip
    mov ax, cx                      ; column = x / 16
    mov cl, 4
    shr ax, cl
    cmp ax, MN_COLS
    jae .out
    mov di, ax
    mov ax, dx                      ; row = y / 16
    shr ax, cl
    cmp ax, MN_COLS
    jae .out
    mov bx, ax                      ; cell = row*9 + column
    add ax, ax
    add ax, ax
    add ax, ax
    add ax, bx
    add ax, di
    mov bx, ax

    cmp byte [mn_flagmode], 0
    jne .flag

    ; --- reveal ---------------------------------------------------------------
    cmp byte [mn_state+bx], MN_S_COVER
    jne .out                        ; flags block reveal; open cells are done
    cmp byte [mn_mode], MN_M_FRESH
    jne .armed
    call mn_place                   ; lazy placement: first click always safe
    mov byte [mn_mode], MN_M_LIVE
.armed:
    cmp byte [mn_mine+bx], 0
    jne .boom
    call mn_reveal                  ; opens + draws the flood
    cmp byte [mn_revealed], MN_SAFE
    jb .out
    mov byte [mn_mode], MN_M_WON    ; all 71 safe cells open
    call mn_draw_status
    jmp .out
.boom:
    mov byte [mn_mode], MN_M_LOST
    mov ax, bx
    mov [mn_boom], al               ; the one that gets the red cell
    call mn_draw_board              ; mode-aware: mines + wrong flags shown
    call mn_draw_status
    jmp .out

    ; --- flag mode --------------------------------------------------------------
.flag:
    cmp byte [mn_state+bx], MN_S_OPEN
    je .out                         ; can't flag an open cell
    cmp byte [mn_state+bx], MN_S_FLAG
    je .unflag
    mov byte [mn_state+bx], MN_S_FLAG
    inc byte [mn_flags]
    jmp .flagupd
.unflag:
    mov byte [mn_state+bx], MN_S_COVER
    dec byte [mn_flags]
.flagupd:
    call mn_draw_cell
    call mn_draw_status             ; mines-remaining counter changed

.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mn_place - place the 10 mines, excluding the first-clicked cell, and build
; the adjacent-mine counts by bumping every placed mine's neighbours
; in:  BX = safe cell index
; out: nothing; clobbers AX, CX, DX, SI, DI (BX preserved)
; -----------------------------------------------------------------------------
mn_place:
    push bp
    call JAPI_GET_TICKS
    call JAPI_SRAND
    mov bp, MN_MINES                ; mines left to place
.next:
    call JAPI_RAND                  ; AX = pseudo-random word
    xor dx, dx
    mov di, MN_CELLS
    div di                          ; DX = AX mod 81
    cmp dx, bx
    je .next                        ; keep the first click safe
    mov di, dx
    cmp byte [mn_mine+di], 0
    jne .next                       ; already a mine there
    mov byte [mn_mine+di], 1
    push bx                         ; bump neighbours' adjacency counts
    mov bx, di
    call mn_nlist                   ; CX = count, mn_nbuf = indices
    mov si, mn_nbuf
    jcxz .placed
.adj:
    mov bl, [si]
    mov bh, 0
    inc byte [mn_adj+bx]
    inc si
    loop .adj
.placed:
    pop bx
    dec bp
    jnz .next
    pop bp
    ret

; -----------------------------------------------------------------------------
; mn_reveal - open cell BX (known covered, unflagged, not a mine) and flood
; through zero-count cells: iterative, explicit queue, no recursion. Cells
; are marked open when enqueued (so nothing enters the queue twice - 81
; words is provably enough) and drawn when dequeued.
; in:  BX = cell index
; out: nothing; [mn_revealed] bumped per cell; clobbers AX, CX, DX, SI
; -----------------------------------------------------------------------------
mn_reveal:
    push bx
    push di
    mov word [mn_qhead], 0
    mov word [mn_qtail], 0
    mov byte [mn_state+bx], MN_S_OPEN
    inc byte [mn_revealed]
    call mn_enq
.pump:
    mov ax, [mn_qhead]
    cmp ax, [mn_qtail]
    je .done
    mov si, ax                      ; dequeue
    shl si, 1
    mov bx, [mn_queue+si]
    inc word [mn_qhead]
    call mn_draw_cell
    cmp byte [mn_adj+bx], 0
    jne .pump                       ; numbered cell: no spreading
    call mn_nlist                   ; CX = count, mn_nbuf = indices
    mov si, mn_nbuf
    jcxz .pump
.nb:
    mov bl, [si]
    mov bh, 0
    cmp byte [mn_state+bx], MN_S_COVER
    jne .skip                       ; flagged or already open
    mov byte [mn_state+bx], MN_S_OPEN
    inc byte [mn_revealed]
    call mn_enq
.skip:
    inc si
    loop .nb
    jmp .pump
.done:
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; mn_enq - append cell BX to the flood queue
; in:  BX = cell index
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_enq:
    push di
    mov di, [mn_qtail]
    shl di, 1
    mov [mn_queue+di], bx
    inc word [mn_qtail]
    pop di
    ret

; -----------------------------------------------------------------------------
; mn_nlist - list the valid neighbours of a cell (3..8 of them)
; in:  BX = cell index
; out: CX = neighbour count, mn_nbuf = neighbour indices (bytes)
; clobbers: AX, DX (BX, SI, DI preserved)
; -----------------------------------------------------------------------------
mn_nlist:
    push bx
    push di
    mov ax, bx
    mov dl, MN_COLS
    div dl                          ; AL = row, AH = column
    xor cx, cx
    mov di, mn_nbuf
    mov dh, -1                      ; row delta: -1, 0, +1
.row:
    mov dl, al
    add dl, dh                      ; neighbour row
    cmp dl, MN_COLS
    jae .rownext                    ; unsigned: catches -1 (255) and 9
    mov bh, -1                      ; column delta: -1, 0, +1
.col:
    mov bl, ah
    add bl, bh                      ; neighbour column
    cmp bl, MN_COLS
    jae .colnext
    cmp dh, 0
    jne .keep
    cmp bh, 0
    je .colnext                     ; skip the cell itself
.keep:
    push ax
    mov al, dl                      ; index = row*9 + column
    add al, al
    add al, al
    add al, al
    add al, dl
    add al, bl
    mov [di], al                    ; (no stosb: ES is not ours to assume)
    inc di
    pop ax
    inc cx
.colnext:
    inc bh
    cmp bh, 2
    jl .col
.rownext:
    inc dh
    cmp dh, 2
    jl .row
    pop di
    pop bx
    ret

; -----------------------------------------------------------------------------
; mn_cellxy - screen position of a cell's top-left pixel
; in:  BX = cell index; uses [mn_ox]/[mn_oy] stashed by the handler
; out: CX = x, DX = y; clobbers AX
; -----------------------------------------------------------------------------
mn_cellxy:
    push bx
    mov ax, bx
    mov bl, MN_COLS
    div bl                          ; AL = row, AH = column
    mov bl, ah
    mov bh, 0
    mov cl, 4
    shl bx, cl                      ; column * 16
    mov cx, [mn_ox]
    add cx, bx
    mov bl, al
    mov bh, 0
    push cx
    mov cl, 4
    shl bx, cl                      ; row * 16
    pop cx
    mov dx, [mn_oy]
    add dx, MN_STRIP_H
    add dx, bx
    pop bx
    ret

; -----------------------------------------------------------------------------
; mn_shape - run a list of coloured fills relative to a cell origin.
; Record: db colour, x1, y1, x2, y2 (cell-relative bytes); 0xFF terminates.
; in:  SI -> shape list, CX = cell x, DX = cell y
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_shape:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, cx                      ; cell left
    mov di, dx                      ; cell top
.rec:
    lodsb                           ; colour (DF=0 per calling convention)
    cmp al, 0xFF
    je .done
    call JAPI_SET_COLOR
    lodsb                           ; x1
    mov ah, 0
    add ax, bp
    push ax
    lodsb                           ; y1
    mov ah, 0
    add ax, di
    push ax
    lodsb                           ; x2
    mov ah, 0
    add ax, bp
    mov cx, ax
    lodsb                           ; y2
    mov ah, 0
    add ax, di
    mov dx, ax
    pop bx                          ; y1
    pop ax                          ; x1
    call JAPI_GFX_FILL
    jmp .rec
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
; mn_draw_cell - draw one cell in its normal in-play state
; in:  BX = cell index
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_draw_cell:
    push ax
    push cx
    push dx
    push si
    call mn_cellxy                  ; CX/DX = cell origin
    cmp byte [mn_state+bx], MN_S_OPEN
    je .open
    mov si, mn_sh_covered
    call mn_shape
    cmp byte [mn_state+bx], MN_S_FLAG
    jne .done
    mov si, mn_sh_flag
    call mn_shape
    jmp .done
.open:
    mov si, mn_sh_open
    call mn_shape
    mov al, [mn_adj+bx]
    or al, al
    jz .done                        ; zero-count cells stay blank
    push bx                         ; pinned digit colour (SPEC.md 23)
    mov bl, al
    mov bh, 0
    mov al, [mn_digcol+bx]
    pop bx
    call JAPI_SET_COLOR
    mov al, [mn_adj+bx]
    add al, '0'
    add cx, 4                       ; centre the 8x8 glyph in the 16px cell
    add dx, 4
    call JAPI_FONT_CHAR
.done:
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mn_draw_cell2 - mode-aware cell renderer: normal while playing; after a
; loss, unflagged mines show as mines (the clicked one on light red),
; wrong flags get a mine + black X, correct flags keep their flag.
; in:  BX = cell index
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_draw_cell2:
    cmp byte [mn_mode], MN_M_LOST
    jne mn_draw_cell
    cmp byte [mn_mine+bx], 0
    je .nomine
    cmp byte [mn_state+bx], MN_S_FLAG
    je mn_draw_cell                 ; correctly flagged mine keeps its flag
    jmp mn_draw_minecell
.nomine:
    cmp byte [mn_state+bx], MN_S_FLAG
    je mn_draw_wrongflag
    jmp mn_draw_cell

; -----------------------------------------------------------------------------
; mn_draw_minecell - an exposed mine (loss): black disc with spokes on a
; light gray cell, or light red for the mine that was clicked
; in:  BX = cell index
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_draw_minecell:
    push ax
    push cx
    push dx
    push si
    call mn_cellxy
    mov si, mn_sh_bggray
    mov ax, bx
    cmp al, [mn_boom]
    jne .bg
    mov si, mn_sh_bgred
.bg:
    call mn_shape
    mov si, mn_sh_mine
    call mn_shape
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mn_draw_wrongflag - a flag with no mine under it (loss): mine + black X
; in:  BX = cell index
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_draw_wrongflag:
    push ax
    push cx
    push dx
    push si
    push di
    call mn_cellxy
    mov si, mn_sh_bggray
    call mn_shape
    mov si, mn_sh_mine
    call mn_shape
    mov al, CBLACK
    call JAPI_SET_COLOR
    mov di, 0                       ; two 10px diagonals, corner to corner
.px:
    push cx
    push dx
    add cx, di
    add cx, 3
    add dx, di
    add dx, 3
    call JAPI_GFX_PIXEL             ; (3+i, 3+i)
    pop dx
    pop cx
    push cx
    push dx
    add cx, 12
    sub cx, di
    add dx, di
    add dx, 3
    call JAPI_GFX_PIXEL             ; (12-i, 3+i)
    pop dx
    pop cx
    inc di
    cmp di, 10
    jb .px
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mn_draw_board - repaint all 81 cells (mode-aware)
; in:  nothing (uses [mn_ox]/[mn_oy])
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_draw_board:
    push bx
    mov bx, 0
.cell:
    call mn_draw_cell2
    inc bx
    cmp bx, MN_CELLS
    jb .cell
    pop bx
    ret

; -----------------------------------------------------------------------------
; mn_draw_status - repaint the status strip: light gray band, the
; mines-remaining counter at the left (10 minus flags placed, clamped at 0),
; and the centred mode text
; in:  nothing (uses [mn_ox]/[mn_oy] and the game state)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
mn_draw_status:
    push ax
    push bx
    push cx
    push dx
    push si
    mov al, CLGRAY
    call JAPI_SET_COLOR
    mov ax, [mn_ox]
    mov bx, [mn_oy]
    mov cx, ax
    add cx, MN_BOARD_W - 1
    mov dx, bx
    add dx, MN_STRIP_H - 1
    call JAPI_GFX_FILL

    mov al, CBLACK                  ; counter, clamped at 0
    call JAPI_SET_COLOR
    mov al, MN_MINES
    sub al, [mn_flags]
    jnc .clamped
    mov al, 0
.clamped:
    mov cx, [mn_ox]
    add cx, 4
    mov dx, [mn_oy]
    add dx, 6
    cmp al, 10                      ; only ever 0..10: "10" or one digit
    jb .one
    mov al, '1'
    call JAPI_FONT_CHAR
    add cx, 8
    mov al, '0'
    jmp .putc
.one:
    add al, '0'
.putc:
    call JAPI_FONT_CHAR

    mov si, mn_s_boom               ; centred mode text
    cmp byte [mn_mode], MN_M_LOST
    je .have
    mov si, mn_s_win
    cmp byte [mn_mode], MN_M_WON
    je .have
    cmp byte [mn_flagmode], 0
    je .done                        ; playing, flag mode off: blank
    mov si, mn_s_flag
.have:
    call JAPI_FONT_WIDTH            ; AX = pixel width
    mov cx, MN_BOARD_W
    sub cx, ax
    shr cx, 1
    add cx, [mn_ox]
    mov dx, [mn_oy]
    add dx, 6
    call JAPI_FONT_STR
.done:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
mn_tpl:
    dw 240, 120, 146, 183           ; x, y, w, h -> content 144 x 164
    dw mn_ttl, mn_paint, mn_onkey, mn_onclick

mn_ttl:     db 'Minesweeper', 0
mn_s_flag:  db 'F=FLAG', 0
mn_s_boom:  db 'BOOM! N=NEW', 0
mn_s_win:   db 'YOU WIN!', 0

; pinned digit colours, indexed by adjacent-mine count 1..8 (SPEC.md 23)
mn_digcol:
    db 0                            ; [0] unused
    db CBLUE, CGREEN, CRED, CMAGENTA, CBROWN, CCYAN, CBLACK, CDGRAY

; --- cell shape lists: db colour, x1, y1, x2, y2 (cell-relative); 0xFF ends ----
mn_sh_covered:                      ; light gray face, 2px bevels
    db CLGRAY,  0,  0, 15, 15
    db CWHITE,  0,  0, 15,  1       ; top bevel
    db CWHITE,  0,  0,  1, 15       ; left bevel
    db CDGRAY,  0, 14, 15, 15       ; bottom bevel
    db CDGRAY, 14,  0, 15, 15       ; right bevel
    db 0xFF

mn_sh_flag:                         ; overlay on a covered cell
    db CBLACK,  8,  3,  8, 11       ; mast
    db CBLACK,  6, 12, 10, 12       ; base
    db CLRED,   4,  3,  7,  7       ; pennant
    db 0xFF

mn_sh_open:                         ; white face, 1px dark gray grid border
    db CWHITE,  0,  0, 15, 15
    db CDGRAY,  0,  0, 15,  0
    db CDGRAY,  0,  0,  0, 15
    db CDGRAY, 15,  0, 15, 15
    db CDGRAY,  0, 15, 15, 15
    db 0xFF

mn_sh_bggray:                       ; loss backgrounds under the mine glyph
    db CLGRAY,  0,  0, 15, 15
    db 0xFF

mn_sh_bgred:
    db CLRED,   0,  0, 15, 15
    db 0xFF

mn_sh_mine:                         ; black disc with spokes
    db CBLACK,  5,  5, 10, 10       ; disc core
    db CBLACK,  6,  4,  9,  4       ; rounded top / bottom / left / right
    db CBLACK,  6, 11,  9, 11
    db CBLACK,  4,  6,  4,  9
    db CBLACK, 11,  6, 11,  9
    db CBLACK,  7,  2,  8, 13       ; vertical spoke
    db CBLACK,  2,  7, 13,  8       ; horizontal spoke
    db CBLACK,  3,  3,  4,  4       ; diagonal spoke nubs
    db CBLACK, 11,  3, 12,  4
    db CBLACK,  3, 11,  4, 12
    db CBLACK, 11, 11, 12, 12
    db 0xFF

    JOP_BSS MN_BSS_TOTAL
    JOP_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = fresh game: covered board, no mines placed (MN_M_FRESH), flag
; mode off, nothing revealed. mn_mine/mn_state/mn_adj must stay contiguous -
; the 'N' handler wipes all three as one 243-byte run.
mn_mine     equ jop_image_end + 0   ; 81 bytes: 1 = mine here
mn_state    equ jop_image_end + 81  ; 81 bytes: MN_S_COVER / FLAG / OPEN
mn_adj      equ jop_image_end + 162 ; 81 bytes: adjacent-mine counts 0..8
mn_queue    equ jop_image_end + 243 ; 81 words: flood-fill queue
mn_qhead    equ jop_image_end + 405 ; word: queue read index
mn_qtail    equ jop_image_end + 407 ; word: queue write index
mn_ox       equ jop_image_end + 409 ; word: content left (per handler call)
mn_oy       equ jop_image_end + 411 ; word: content top
mn_flags    equ jop_image_end + 413 ; byte: flags currently placed
mn_mode     equ jop_image_end + 414 ; byte: MN_M_*
mn_flagmode equ jop_image_end + 415 ; byte: 1 = clicks flag instead of reveal
mn_revealed equ jop_image_end + 416 ; byte: open safe cells (win at 71)
mn_boom     equ jop_image_end + 417 ; byte: the mine cell that was clicked
mn_nbuf     equ jop_image_end + 418 ; 8 bytes: mn_nlist output
                                    ; total 426 = MN_BSS_TOTAL
