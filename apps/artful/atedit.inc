; =============================================================================
; os8088 - apps/artful/atedit.inc
;
; ArtfulType's editing engine (SPEC.md 46.3): the keystroke dispatch, caret
; navigation and selection (shift-click, shift-arrows and a sol_drag-style
; drag-select that XORs only the delta range per pass), the app-local
; clipboard, snapshot undo/redo in a heap claim, and the scroll-bar
; interactions. The Style/File/View commands live in atcmd.inc.
;
; The performance contract (SPEC.md 46.1): a printable keystroke costs a
; gap-buffer store, a one-paragraph relayout, and the repaint of the lines
; that paragraph occupies - each one blit. Nothing on the typing path walks
; the whole document.
; =============================================================================

; -----------------------------------------------------------------------------
; at_shiftctl - read the BIOS shift flags (0040:0017)
; out: [at_shift] = 1 if either Shift is down, else 0; preserves all
; The BIOS data area is BIOS state, not kernel memory - the same class of
; read as paint's ROM font probe (SPEC.md 42).
; -----------------------------------------------------------------------------
at_shiftctl:
    push ax
    push es
    mov ax, 0x0040
    mov es, ax
    mov al, [es:0x17]
    and al, 3
    jz .st
    mov al, 1
.st:
    mov [at_shift], al
    pop es
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_ed_key - the fullscreen editor's key dispatch
; in:  AL = ascii, AH = scan (int 16h's pair, via W_ONKEY); lock held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
at_ed_key:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call at_shiftctl
    mov byte [at_cphase], 0         ; a key relights the caret
    cmp al, 27
    jne .notesc                     ; Esc leaves fullscreen (SPEC.md 11.2's
    call at_fs_exit                 ; convention; the document survives)
    jmp .out
.notesc:
    or al, al                       ; extended keys: AL 0 (AT BIOS) or E0h
    jz .ext
    cmp al, 0E0h
    je .ext
    cmp al, 8
    jne .notbs
    cmp ah, 0x0E                    ; Backspace by scan (^H is scan 23h)
    jne .out
    call at_backspace
    jmp .out
.notbs:
    cmp al, 13
    jne .notcr
    cmp ah, 0x1C                    ; Enter by scan (^M is scan 32h)
    jne .out
    mov al, AT_NL
    call at_type
    jmp .out
.notcr:
    cmp al, 9
    jne .nottab
    cmp ah, 0x0F                    ; Tab: four spaces; ^I (scan 17h) falls
    jne .ctl                        ; through to the Ctrl map (italic)
    mov cx, 4
.tab:
    push cx
    mov al, ' '
    call at_type
    pop cx
    loop .tab
    jmp .out
.nottab:
    cmp al, 32
    jb .ctl
    cmp al, 126
    ja .out
    call at_type
    jmp .out
.ctl:
    call at_ctrlkey
    jmp short .out
.ext:
    call at_nav
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_ctrlkey - the ^letter shortcut map (AL = 1..26)
; ^A select all, ^B bold, ^C copy, ^I italic, ^K code, ^L link, ^N new,
; ^O open, ^Q quit, ^S save, ^V paste, ^X cut, ^Z undo (Shift: redo) -
; main.c's table with Ctrl standing in for Cmd.
; -----------------------------------------------------------------------------
at_ctrlkey:
    push ax
    push bx
    push si
    mov si, at_ctltab
.scan:
    mov bx, [si]
    or bl, bl
    jz .out
    cmp al, bl
    je .hit
    add si, 2
    jmp short .scan
.hit:
    mov al, bh
    cmp al, AT_CMD_UNDO
    jne .go
    cmp byte [at_shift], 0
    je .go
    mov al, AT_CMD_REDO
.go:
    call at_docmd
.out:
    pop si
    pop bx
    pop ax
    ret

at_ctltab:                          ; pairs: ctrl code, command
    db 1,  AT_CMD_SELALL
    db 2,  AT_CMD_BOLD
    db 3,  AT_CMD_COPY
    db 9,  AT_CMD_ITALIC
    db 11, AT_CMD_CODE
    db 12, AT_CMD_LINK
    db 14, AT_CMD_NEW
    db 15, AT_CMD_OPEN
    db 17, AT_CMD_QUIT
    db 19, AT_CMD_SAVE
    db 22, AT_CMD_PASTE
    db 24, AT_CMD_CUT
    db 26, AT_CMD_UNDO
    db 0, 0

; -----------------------------------------------------------------------------
; at_type - one printable character (or newline) at the caret
; in:  AL = the byte
; -----------------------------------------------------------------------------
at_type:
    push ax
    push bx
    push cx
    push dx
    cmp al, AT_NL                   ; guard the line table before a newline
    jne .room                       ; can overflow it
    mov bx, [at_nlines]
    cmp bx, AT_MAXLN-8
    jae .out
.room:
    cmp byte [at_typrun], 0
    jne .run
    call at_undo_push
    mov byte [at_typrun], 1
.run:
    mov cx, [at_sela]
    cmp cx, [at_selb]
    je .nosel
    push ax
    call at_selget                  ; AX = lo, DX = hi
    mov cx, dx
    sub cx, ax                      ; CX = deleted count
    call at_delrange                ; caret = lo, gap parked at lo
    mov bx, ax                      ; BX = the edit position
    pop ax
    call at_ins
    jc .out
    mov dx, 1
    sub dx, cx                      ; delta = 1 - deleted
    mov ax, bx
    jmp short .apply
.nosel:
    push ax
    mov ax, [at_caret]
    call at_gapto
    pop ax                          ; the char back - at_ins stores AL
    call at_ins
    jc .out
    mov ax, [at_caret]
    dec ax
    mov dx, 1
.apply:
    push ax
    mov ax, [at_caret]
    mov [at_sela], ax
    mov [at_selb], ax
    pop ax
    call at_apply_edit
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_backspace / at_delkey - delete backward / forward
; -----------------------------------------------------------------------------
at_run_open:                        ; open a typing run (undo push once)
    cmp byte [at_typrun], 0
    jne .done
    call at_undo_push
    mov byte [at_typrun], 1
.done:
    ret

at_backspace:
    push ax
    push cx
    push dx
    mov ax, [at_sela]
    cmp ax, [at_selb]
    jne .sel
    mov ax, [at_caret]
    or ax, ax
    jz .out
    call at_run_open
    mov dx, ax
    dec ax
    call at_delrange
    mov ax, [at_caret]              ; the anchor rides along, like every
    mov [at_sela], ax               ; other caret-moving edit - a stale one
    mov [at_selb], ax               ; mis-anchors the next shift-extend
    mov dx, -1
    call at_apply_edit
    jmp short .out
.sel:
    call at_run_open
    call at_kill_sel
.out:
    pop dx
    pop cx
    pop ax
    ret

at_delkey:
    push ax
    push cx
    push dx
    mov ax, [at_sela]
    cmp ax, [at_selb]
    jne .sel
    mov ax, [at_caret]
    push ax
    call at_dlen
    mov dx, ax
    pop ax
    cmp ax, dx
    jae .out
    call at_run_open
    mov dx, ax
    inc dx
    call at_delrange
    mov dx, -1
    call at_apply_edit
    jmp short .out
.sel:
    call at_run_open
    call at_kill_sel
.out:
    pop dx
    pop cx
    pop ax
    ret

; at_kill_sel - delete the selection (undo already pushed by the caller)
at_kill_sel:
    push ax
    push cx
    push dx
    call at_selget                  ; AX = lo, DX = hi
    mov cx, dx
    sub cx, ax
    call at_delrange
    mov dx, [at_caret]
    mov [at_sela], dx
    mov [at_selb], dx
    mov dx, cx
    neg dx
    call at_apply_edit
    pop dx
    pop cx
    pop ax
    ret

; at_selget - AX = selection low, DX = high (caret twice when none)
at_selget:
    mov ax, [at_sela]
    mov dx, [at_selb]
    cmp ax, dx
    jbe .ok
    xchg ax, dx
.ok:
    ret

; -----------------------------------------------------------------------------
; at_apply_edit - relayout + repaint after a buffer change
; in:  AX = lowest touched logical offset, DX = signed byte delta
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
at_apply_edit:
    push ax
    push bx
    push cx
    push dx
    mov byte [at_dirty], 1
    call at_caret_off
    mov cx, [at_nlines]             ; CX = the old count
    mov byte [at_rlfull], 0
    call at_relayout
    call at_cline_span
    cmp byte [at_rlfull], 0
    jne .full
    cmp cx, [at_nlines]
    jne .below
    ; same line count: the changed lines are the staged window, plus the
    ; caret's paragraph (raw/styled state follows the caret)
    mov bx, [at_dfrom]
    mov dx, bx
    add dx, [at_rlk]
    dec dx
    cmp dx, [at_cll1]
    jae .rng
    mov dx, [at_cll1]
.rng:
    call at_redraw_range
    jmp short .see
.below:
    call at_maxtop                  ; an incremental delete can strand a
    mov bx, ax                      ; bottom-scrolled view too (the .full
    mov ax, [at_top]                ; clamp, same reason)
    cmp ax, bx
    jbe .bok
    mov [at_top], bx
    jmp short .fdraw                ; the view moved: everything repaints
.bok:
    mov bx, [at_dfrom]
    call at_redraw_below
    call at_sbar
    jmp short .see
.full:
    call at_maxtop                  ; a bulk edit can strand the view
    mov bx, ax
    mov ax, [at_top]
    cmp ax, bx
    jbe .fdraw
    mov [at_top], bx
.fdraw:
    call at_draw_text
    call at_sbar
.see:
    call at_seecaret
    call at_caret_on
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_redraw_range - draw every visible line in [BX .. DX]
; -----------------------------------------------------------------------------
at_redraw_range:
    push ax
    push bx
    push dx
    cmp bx, [at_top]
    jae .go
    mov bx, [at_top]
.go:
    cmp bx, [at_nlines]
    jae .out
    cmp bx, dx
    ja .out
    call at_line_y
    jc .out                         ; BX >= top, so CF means below the view
    call at_draw_line
    inc bx
    jmp short .go
.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_redraw_below - erase from line BX's y down, then draw to the bottom
; -----------------------------------------------------------------------------
at_redraw_below:
    push ax
    push bx
    push cx
    push dx
    cmp bx, [at_top]
    jae .from
    call at_draw_text
    jmp short .out
.from:
    call at_line_y
    jc .out                         ; below the view: nothing shows
    mov [at_rby], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [at_tx0]
    push bx
    mov bx, [at_rby]
    mov cx, ax
    add cx, [at_tw]
    dec cx
    mov dx, [at_ty1]
    dec dx
    call OSAPI_GFX_FILL
    pop bx
.draw:
    cmp bx, [at_nlines]
    jae .out
    call at_line_y
    jc .out
    call at_draw_line
    inc bx
    jmp short .draw
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_nav - the extended keys: arrows, Home/End, PgUp/PgDn, Del
; in:  AH = scan; [at_shift] fresh
; -----------------------------------------------------------------------------
at_nav:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [at_typrun], 0
    cmp ah, 0x53                    ; Del edits; everything else moves
    jne .move
    call at_delkey
    jmp .out
.move:
    mov cx, [at_cll0]               ; latch the pre-move state the repaint
    mov [at_ocll0], cx              ; decision needs
    mov cx, [at_cll1]
    mov [at_ocll1], cx
    mov cx, [at_selb]
    mov [at_oselb], cx
    mov si, [at_caret]              ; SI = the moving point
    cmp ah, 0x4B
    je .left
    cmp ah, 0x4D
    je .right
    cmp ah, 0x47
    je .home
    cmp ah, 0x4F
    je .end
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    cmp ah, 0x49
    je .pgup
    cmp ah, 0x51
    je .pgdn
    jmp .out
.left:
    call at_collapse_lo
    jc .have
    or si, si
    jz .have
    dec si
    jmp short .have
.right:
    call at_collapse_hi
    jc .have
    call at_dlen
    cmp si, ax
    jae .have
    inc si
    jmp short .have
.home:
    mov ax, si
    call at_line_of
    call at_lrange                  ; AX = line start
    mov si, ax
    jmp short .have
.end:
    mov ax, si
    call at_line_of
    call at_lrange                  ; DX = line end
    mov si, dx
    jmp short .have
.up:
    mov cx, 1
    call at_updown_up
    jmp short .have
.down:
    mov cx, 1
    call at_updown_dn
    jmp short .have
.pgup:
    call at_vislines
    mov cx, ax
    call at_updown_up
    jmp short .have
.pgdn:
    call at_vislines
    mov cx, ax
    call at_updown_dn
.have:
    cmp ah, 0x48                    ; the goal column survives only
    je .keep                        ; vertical motion
    cmp ah, 0x50
    je .keep
    cmp ah, 0x49
    je .keep
    cmp ah, 0x51
    je .keep
    mov word [at_goal], 0FFFFh
.keep:
    mov [at_caret], si
    cmp byte [at_shift], 0
    jne .extend
    mov [at_sela], si
    mov [at_selb], si
    jmp short .paint
.extend:
    mov [at_selb], si               ; the anchor stays; a fresh anchor is
                                    ; the old caret, already in at_sela
.paint:
    call at_nav_paint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; a plain (unshifted) move with a live selection collapses to its edge
at_collapse_lo:
    push ax
    push dx
    cmp byte [at_shift], 0
    jne .no
    mov ax, [at_sela]
    cmp ax, [at_selb]
    je .no
    call at_selget
    mov si, ax
    stc
    jmp short .out
.no:
    clc
.out:
    pop dx
    pop ax
    ret
at_collapse_hi:
    push ax
    push dx
    cmp byte [at_shift], 0
    jne .no
    mov ax, [at_sela]
    cmp ax, [at_selb]
    je .no
    call at_selget
    mov si, dx
    stc
    jmp short .out
.no:
    clc
.out:
    pop dx
    pop ax
    ret

; at_updown_up / at_updown_dn - move SI up/down CX visual lines by goal x
at_updown_up:
    push ax
    push bx
    push dx
    call at_goalx
    mov ax, si
    call at_line_of
.u:
    or bx, bx
    jz .place
    dec bx
    loop .u
.place:
    call at_colseek
    pop dx
    pop bx
    pop ax
    ret
at_updown_dn:
    push ax
    push bx
    push dx
    call at_goalx
    mov ax, si
    call at_line_of
.d:
    mov ax, [at_nlines]
    dec ax
    cmp bx, ax
    jae .place
    inc bx
    loop .d
.place:
    call at_colseek
    pop dx
    pop bx
    pop ax
    ret

; at_goalx - ensure [at_goal] holds the caret's x (set once per run)
at_goalx:
    push ax
    push bx
    cmp word [at_goal], 0FFFFh
    jne .out
    mov ax, [at_caret]
    call at_line_of
    call at_parse
    mov ax, [at_caret]
    sub ax, [at_pstart]
    mov bx, ax
    shl bx, 1
    mov ax, [bx+at_xmap]
    mov [at_goal], ax
.out:
    pop bx
    pop ax
    ret

; at_colseek - SI = the position in line BX whose x best matches [at_goal]
at_colseek:
    push ax
    push bx
    push cx
    push dx
    call at_parse
    xor si, si
    mov cx, [at_pcnt]
.walk:
    cmp si, cx
    jae .done
    mov bx, si
    shl bx, 1
    mov ax, [bx+at_xmap]
    mov dx, [bx+at_xmap+2]
    add ax, dx
    rcr ax, 1                       ; the cell's midpoint (carry-safe)
    cmp [at_goal], ax
    jb .done
    inc si
    jmp short .walk
.done:
    add si, [at_pstart]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_vislines - AX = how many lines the view shows from [at_top] (>= 1)
at_vislines:
    push bx
    push cx
    push dx
    mov bx, [at_top]
    xor cx, cx
    mov ax, [at_ty0]
.w:
    cmp bx, [at_nlines]
    jae .done
    push ax
    call at_lgeom
    pop ax
    add ax, dx
    inc cx
    inc bx
    cmp ax, [at_ty1]
    jb .w
.done:
    mov ax, cx
    or ax, ax
    jnz .out
    inc ax
.out:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_nav_paint - repaint after a caret/selection move
; Redraws only what changed: the caret's old and new paragraphs (whose
; raw/styled state follows the caret in Writer mode) and the selection
; delta (as line redraws, which fold the XOR back in exactly).
; -----------------------------------------------------------------------------
at_nav_paint:
    push ax
    push bx
    push cx
    push dx
    call at_caret_off
    call at_cline_span
    mov bx, [at_cll0]               ; BX..DX = the union range
    mov dx, [at_cll1]
    mov ax, [at_ocll0]
    cmp ax, bx
    jae .lo
    mov bx, ax
.lo:
    mov ax, [at_ocll1]
    cmp ax, dx
    jbe .hi
    mov dx, ax
.hi:
    ; fold in the selection delta's lines
    mov ax, [at_oselb]
    cmp ax, [at_selb]
    je .nodelta
    mov cx, ax
    mov ax, [at_selb]
    cmp cx, ax
    jbe .ord
    xchg cx, ax
.ord:
    mov [at_nvlo], cx
    mov [at_nvhi], ax
    mov ax, [at_nvlo]
    push bx
    call at_line_of
    mov cx, bx
    pop bx
    cmp cx, bx
    jae .dl
    mov bx, cx
.dl:
    mov ax, [at_nvhi]
    push bx
    call at_line_of
    mov cx, bx
    pop bx
    cmp cx, dx
    jbe .nodelta
    mov dx, cx
.nodelta:
    ; anything to redraw at all?
    mov ax, [at_oselb]
    cmp ax, [at_selb]
    jne .lines
    cmp byte [at_writer], 0
    je .skip                        ; markdown, no selection change: caret
    mov ax, [at_cll0]               ; writer: only if the paragraph moved
    cmp ax, [at_ocll0]
    jne .lines
    mov ax, [at_cll1]
    cmp ax, [at_ocll1]
    je .skip
.lines:
    call at_redraw_range
.skip:
    call at_seecaret
    call at_caret_on
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_xy2log - map a screen point to a logical position
; in:  CX = x, DX = y
; out: AX = logical position; preserves all else
; -----------------------------------------------------------------------------
at_xy2log:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, cx
    mov di, dx
    mov bx, [at_top]
    mov ax, [at_ty0]
    cmp di, ax
    jb .gotline                     ; above the text: the top line
.scan:
    mov cx, [at_nlines]
    dec cx
    cmp bx, cx
    jae .gotline
    push ax
    call at_lgeom                   ; DX = rowh
    pop ax
    add ax, dx
    cmp di, ax
    jb .gotline
    cmp ax, [at_ty1]
    jae .gotline                    ; below the view: the last visible line
    inc bx
    jmp short .scan
.gotline:
    call at_parse
    sub si, [at_tx0]
    jns .x0
    xor si, si
.x0:
    xor di, di                      ; DI = index
    mov cx, [at_pcnt]
.col:
    cmp di, cx
    jae .done
    mov bx, di
    shl bx, 1
    mov ax, [bx+at_xmap]
    mov dx, [bx+at_xmap+2]
    add ax, dx
    rcr ax, 1
    cmp si, ax
    jb .done
    inc di
    jmp short .col
.done:
    mov ax, [at_pstart]
    add ax, di
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; at_click_text - press in the text region: place the caret, drag-select
; in:  CX = x, DX = y; gfx lock held (W_ONCLICK)
; -----------------------------------------------------------------------------
at_click_text:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [at_typrun], 0
    mov byte [at_cphase], 0
    call at_shiftctl
    call at_xy2log                  ; AX = the pressed position
    cmp byte [at_shift], 0
    je .plain
    mov [at_selb], ax               ; shift-click extends from the anchor
    mov [at_caret], ax
    jmp short .painted
.plain:
    mov [at_caret], ax
    mov [at_sela], ax
    mov [at_selb], ax
.painted:
    call at_nav_paint_click
    mov byte [at_drag], 1
.pass:
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    call OSAPI_MOUSE                ; CX = x, DX = y, AL = buttons
    test al, 1
    jz .up
    call at_xy2log
    cmp ax, [at_selb]
    je .pass
    mov si, [at_selb]               ; XOR exactly the [old, new) delta
    mov [at_selb], ax
    mov [at_caret], ax
    mov dx, ax
    mov ax, si
    call at_xor_range
    jmp short .pass
.up:
    mov byte [at_drag], 0
    call at_nav_paint_click         ; settle raw/styled + caret at the end
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the click path's repaint: at_nav_paint with "old" = the settled state
at_nav_paint_click:
    push ax
    mov ax, [at_cll0]
    mov [at_ocll0], ax
    mov ax, [at_cll1]
    mov [at_ocll1], ax
    mov ax, [at_selb]
    mov [at_oselb], ax              ; no delta: the ranges alone decide
    call at_nav_paint
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_xor_range - XOR-invert logical range [AX, DX) across visible lines
; The drag loop's delta painter: per intersecting line, parse and XOR the
; mapped x span - the same mapping at_selxor uses, so a later full redraw
; folds identical pixels back in.
; -----------------------------------------------------------------------------
at_xor_range:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp ax, dx
    je .out
    jb .ord
    xchg ax, dx
.ord:
    mov [at_xrlo], ax
    mov [at_xrhi], dx
    call at_line_of                 ; BX = lo's line (AX = lo)
.l:
    cmp bx, [at_nlines]
    jae .out
    call at_lrange                  ; AX = line start
    cmp ax, [at_xrhi]
    jae .out                        ; starts past the range: done
    call at_line_y
    jc .chk                         ; not visible; later lines may be
    mov [at_xry], ax
    call at_parse
    ; clamp [lo, hi) to the slice
    mov ax, [at_xrlo]
    mov dx, [at_xrhi]
    mov cx, [at_pstart]
    cmp ax, cx
    jae .clo
    mov ax, cx
.clo:
    add cx, [at_pcnt]
    cmp dx, cx
    jbe .chi
    mov dx, cx
.chi:
    sub ax, [at_pstart]
    sub dx, [at_pstart]
    cmp ax, dx
    jae .chk
    push bx
    mov bx, ax
    shl bx, 1
    mov ax, [bx+at_xmap]
    mov bx, dx
    shl bx, 1
    mov dx, [bx+at_xmap]
    pop bx
    cmp ax, dx
    je .chk
    add ax, [at_tx0]
    add dx, [at_tx0]
    dec dx
    mov cx, dx                      ; CX = x2 inclusive
    push bx
    mov bx, [at_xry]                ; y1
    mov dx, bx
    add dx, [at_prh]
    dec dx
    cmp dx, [at_ty1]
    jb .yok
    mov dx, [at_ty1]
    dec dx
.yok:
    call OSAPI_GFX_XOR_FILL
    pop bx
.chk:
    inc bx
    ; walked past hi's line?
    push bx
    mov ax, [at_xrhi]
    call at_line_of
    mov ax, bx
    pop bx
    cmp bx, ax
    jbe .l
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_sb_click - a press in the scroll bar gutter
; in:  DX = y; gfx lock held
; Arrow boxes repeat while held; the shaft pages; the thumb drags live -
; each a level-sampled loop, like everything else here.
; -----------------------------------------------------------------------------
at_sb_click:
    push ax
    push bx
    push cx
    push dx
    push si
    call at_maxtop
    or ax, ax
    jz .out                         ; no bar is showing
    mov si, ax                      ; SI = maxtop
    mov ax, [at_ty0]
    add ax, 14
    cmp dx, ax
    jbe .up
    mov ax, [at_ty1]
    sub ax, 15
    cmp dx, ax
    jae .down
    call at_thumby
    cmp dx, ax
    jb .pgup
    add ax, 15
    cmp dx, ax
    jbe .thumb
    jmp short .pgdn
.up:
    mov cx, -1
    call at_sb_repeat
    jmp short .out
.down:
    mov cx, 1
    call at_sb_repeat
    jmp short .out
.pgup:
    call at_vislines
    dec ax
    jnz .n1
    inc ax
.n1:
    neg ax
    mov cx, ax
    call at_sb_repeat
    jmp short .out
.pgdn:
    call at_vislines
    dec ax
    jnz .n2
    inc ax
.n2:
    mov cx, ax
    call at_sb_repeat
    jmp short .out
.thumb:
    call at_sb_thumb
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_sb_repeat - step [at_top] by CX (signed), repeating while held
at_sb_repeat:
    push ax
    push bx
    push cx
    push dx
.step:
    push cx
    call at_maxtop
    mov bx, ax
    pop cx
    mov dx, [at_top]
    add dx, cx
    jns .l1
    xor dx, dx
.l1:
    cmp dx, bx
    jbe .go
    mov dx, bx
.go:
    mov ax, dx
    call at_scroll_to
    call at_linger
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    call OSAPI_MOUSE
    test al, 1
    jnz .step
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_sb_thumb - drag the thumb; SI = maxtop
at_sb_thumb:
    push ax
    push bx
    push cx
    push dx
.pass:
    call OSAPI_GFX_UNLOCK
    call OSAPI_TASK_YIELD
    call OSAPI_GFX_LOCK
    call OSAPI_MOUSE                ; DX = y
    test al, 1
    jz .done
    mov ax, [at_ty1]                ; top = (y - ty0 - 23) * maxtop / travel
    sub ax, [at_ty0]
    sub ax, 47
    jle .pass
    mov cx, ax
    mov ax, dx
    sub ax, [at_ty0]
    sub ax, 23                      ; the box + half the thumb
    jns .p
    xor ax, ax
.p:
    mul si
    div cx
    cmp ax, si
    jbe .c
    mov ax, si
.c:
    call at_scroll_to
    jmp short .pass
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; The clipboard (SPEC.md 46.6): one buffer per instance, markdown text -
; a Writer-mode copy is already canonical markdown, which is exactly what
; EncodeSelectionAsMarkdown existed to reconstruct.
; =============================================================================

; at_clipwhere - ES = clip segment, DI = offset, CX = capacity
at_clipwhere:
    mov ax, [at_aseg]
    or ax, ax
    jz .bss
    mov es, ax
    mov di, [at_coff]
    mov cx, [at_csz]
    ret
.bss:
    push ds
    pop es
    mov di, at_clipbss
    mov cx, AT_CLIPBSS
    ret

at_copy:
    push ax
    push bx
    push cx
    push dx
    push di
    push es
    call at_selget
    cmp ax, dx
    je .out
    mov [at_xrlo], ax               ; scratch: survive the clipwhere call
    mov [at_xrhi], dx
    call at_clipwhere
    mov ax, [at_xrlo]
    mov dx, [at_xrhi]
    mov bx, dx
    sub bx, ax
    cmp bx, cx
    ja .big
    mov [at_cliplen], bx
    call at_copyout                 ; [AX, DX) -> ES:DI
    jmp short .out
.big:
    push si
    mov al, AT_MD_ERR
    mov si, at_s_toobig
    call at_alert
    pop si
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

at_cut:
    push ax
    push dx
    call at_selget
    cmp ax, dx
    je .out
    call at_copy
    cmp byte [at_modal], 0          ; the too-big alert aborts the cut
    jne .out
    call at_undo_push
    mov byte [at_typrun], 0
    call at_kill_sel
.out:
    pop dx
    pop ax
    ret

at_paste:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov cx, [at_cliplen]
    jcxz .out
    call at_selget
    mov bx, dx
    sub bx, ax                      ; BX = bytes the selection frees
    mov [at_pastepos], ax
    push ax
    call at_dlen
    mov si, ax
    pop ax
    sub si, bx
    add si, cx                      ; SI = the length afterwards
.fit:
    cmp si, [at_dcap]
    jbe .fits
    call at_dgrow                   ; a paste bigger than the buffer grows it
    jnc .fit                        ; a quantum at a time (SPEC.md 46.9), and
    jmp short .nofit                ; only then gives up
.fits:
    call at_undo_push               ; only a paste that WILL land pays:
    mov byte [at_typrun], 0         ; before the fit check, a refused one
                                    ; cost the whole redo stack for nothing
    cmp bx, 0
    je .ins
    push cx
    call at_delrange                ; AX/DX still = lo/hi
    pop cx
.ins:
    mov ax, [at_pastepos]
    call at_gapto
    call at_clipwhere               ; ES:DI = the source; CX reloaded:
    mov cx, [at_cliplen]
    ; copy ES:DI -> the gap; DS must briefly become the clip segment, so
    ; every cursor is latched in registers first
    mov si, di
    mov di, [at_gs]
    mov ax, es                      ; AX = source segment
    push ds
    mov es, [at_dseg]               ; ES = the document (destination)
    mov ds, ax                      ; DS = the clip
    cld
    push cx
    rep movsb
    pop cx
    pop ds                          ; ours again
    add [at_gs], cx
    add [at_caret], cx
    mov ax, [at_caret]
    mov [at_sela], ax
    mov [at_selb], ax
    mov dx, cx
    sub dx, bx                      ; delta = inserted - deleted
    mov ax, [at_pastepos]
    call at_apply_edit
    jmp short .out
.nofit:
    mov al, AT_MD_ERR
    mov si, at_s_nofit
    call at_alert
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

at_selall:
    push ax
    mov byte [at_typrun], 0
    mov word [at_sela], 0
    call at_dlen
    mov [at_selb], ax
    mov [at_caret], ax
    call at_caret_off
    call at_cline_span
    call at_draw_text
    pop ax
    ret

; =============================================================================
; Undo / redo (SPEC.md 46.6): whole-document snapshots (app.h's design -
; canonical text round-trips styling for free) in one heap claim, pushed
; per typing run and per command. Compacting stacks: entries back-to-back,
; the oldest dropped by an in-segment slide when space runs out. No claim =
; the feature quietly reports itself unavailable through the menus' gray.
; =============================================================================

; at_undo_init - one-time heap claim (first fullscreen entry)
at_undo_init:
    push ax
    push bx
    push cx
    cmp word [at_aseg], 0
    jne .out
    mov ax, 64                      ; 64KB: 24K undo + 24K redo + 16K clip
    mov bx, 24576
    mov cx, 16384
    call at_ualloc
    jnc .out
    mov ax, 32
    mov bx, 12288
    mov cx, 8192
    call at_ualloc
    jnc .out
    mov ax, 16
    mov bx, 6144
    mov cx, 4096
    call at_ualloc
.out:
    pop cx
    pop bx
    pop ax
    ret

; at_ualloc - AX = KB wanted; BX = per-stack bytes; CX = clip bytes
at_ualloc:
    push dx
    call OSAPI_MEM_CLAIM            ; AX KB -> DX segment (SPEC.md 50.3)
    jc .no
    mov [at_aseg], dx
    push ax                         ; the arena moves too (SPEC.md 66.5.7):
    mov ax, at_reloc                ; [at_coff]/[at_usz]/[at_csz] are OFFSETS
    call OSAPI_MEM_MOVABLE          ; and spans within it, so at_reloc has one
    pop ax                          ; more word to fix and nothing else
    mov [at_usz], bx
    mov dx, bx
    add dx, bx
    mov [at_coff], dx
    mov [at_csz], cx
    clc
.no:
    pop dx
    ret

; at_undo_push - snapshot the document onto the undo stack; clears redo
at_undo_push:
    push ax
    mov al, 0
    call at_snap_push
    mov word [at_rcnt], 0           ; a fresh edit invalidates redo
    mov word [at_rtop], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_snap_push - push a document snapshot onto stack AL (0 undo / 1 redo)
; A snapshot: len, caret, sela, selb, then len bytes of text.
; -----------------------------------------------------------------------------
at_snap_push:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    cmp word [at_aseg], 0
    je .out
    mov [at_snstk], al
    call at_snvars                  ; stage the stack's books
    call at_dlen
    mov si, ax                      ; SI = document length
    add ax, 8                       ; AX = entry size
    cmp ax, [at_usz]
    ja .wipe                        ; can never fit: drop the whole stack
.fit:
    mov cx, [at_sntop]
    add cx, ax
    cmp cx, [at_usz]
    jbe .room
    call at_sndrop
    jmp short .fit
.room:
    cmp word [at_sncnt], AT_UMAX
    jb .slot
    call at_sndrop
.slot:
    ; write header + text at the staged top
    mov es, [at_aseg]
    mov di, [at_sntop]
    add di, [at_snbase]
    cld
    mov ax, si
    stosw
    mov ax, [at_caret]
    stosw
    mov ax, [at_sela]
    stosw
    mov ax, [at_selb]
    stosw
    xor ax, ax
    mov dx, si
    call at_copyout                 ; the whole document -> ES:DI
    ; record the entry
    mov bx, [at_sncnt]
    shl bx, 1
    add bx, [at_snoffs]
    mov ax, [at_sntop]
    mov [bx], ax
    inc word [at_sncnt]
    add ax, si
    add ax, 8
    mov [at_sntop], ax
    call at_snstore
    jmp short .out
.wipe:
    mov word [at_sncnt], 0
    mov word [at_sntop], 0
    call at_snstore
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; the two stacks' books, staged into common scratch and back
at_snvars:
    push ax
    cmp byte [at_snstk], 0
    jne .redo
    mov ax, [at_ucnt]
    mov [at_sncnt], ax
    mov ax, [at_utop]
    mov [at_sntop], ax
    mov word [at_snbase], 0
    mov word [at_snoffs], at_uoffs
    pop ax
    ret
.redo:
    mov ax, [at_rcnt]
    mov [at_sncnt], ax
    mov ax, [at_rtop]
    mov [at_sntop], ax
    mov ax, [at_usz]
    mov [at_snbase], ax
    mov word [at_snoffs], at_roffs
    pop ax
    ret
at_snstore:
    push ax
    cmp byte [at_snstk], 0
    jne .redo
    mov ax, [at_sncnt]
    mov [at_ucnt], ax
    mov ax, [at_sntop]
    mov [at_utop], ax
    pop ax
    ret
.redo:
    mov ax, [at_sncnt]
    mov [at_rcnt], ax
    mov ax, [at_sntop]
    mov [at_rtop], ax
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_sndrop - evict the staged stack's oldest entry (sncnt > 0)
; DS stays OURS throughout; the claim is reached through ES overrides.
; -----------------------------------------------------------------------------
at_sndrop:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp word [at_sncnt], 0
    je .done
    mov bx, [at_snoffs]
    mov ax, [at_sntop]              ; drop-size when only one entry lives
    cmp word [at_sncnt], 1
    je .sz
    mov ax, [bx+2]                  ; else: the second entry's offset
.sz:
    ; slide [ax .. sntop) down to the slice base
    mov es, [at_aseg]
    mov si, ax
    add si, [at_snbase]
    mov di, [at_snbase]
    mov cx, [at_sntop]
    sub cx, ax
    jcxz .book
.slide:
    push ax
    mov al, [es:si]
    mov [es:di], al
    inc si
    inc di
    pop ax
    loop .slide
.book:
    dec word [at_sncnt]
    sub [at_sntop], ax
    mov cx, [at_sncnt]
    jcxz .done
.shift:                             ; the offsets queue moves left, less AX
    mov si, [bx+2]
    sub si, ax
    mov [bx], si
    add bx, 2
    loop .shift
.done:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; at_undo / at_redo - the two commands
at_undo:
    push ax
    cmp word [at_ucnt], 0
    je .out
    call at_dlen                    ; the take DESTROYS the current text,
    add ax, 8                       ; so refuse while it cannot first be
    cmp ax, [at_usz]                ; pushed - snap_push would .wipe, and
    ja .out                         ; the only copy would die unwarned
    mov al, 1                       ; current state -> the REDO stack
    call at_snap_push
    mov al, 0
    call at_snap_take
.out:
    pop ax
    ret

at_redo:
    push ax
    cmp word [at_rcnt], 0
    je .out
    call at_dlen                    ; same guard as at_undo
    add ax, 8
    cmp ax, [at_usz]
    ja .out
    mov al, 0                       ; current state -> the UNDO stack,
    call at_snap_push               ; WITHOUT clearing redo
    mov al, 1
    call at_snap_take
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_snap_take - pop stack AL's newest snapshot into the document
; -----------------------------------------------------------------------------
at_snap_take:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov [at_snstk], al
    call at_snvars
    mov cx, [at_sncnt]
    or cx, cx
    jnz .live
    jmp .out
.live:
    dec cx
    mov [at_sncnt], cx
    mov bx, cx
    shl bx, 1
    add bx, [at_snoffs]
    mov si, [bx]                    ; SI = the entry's slice offset
    mov [at_sntop], si              ; the stack top retreats to the entry
    call at_snstore
    add si, [at_snbase]
    mov es, [at_aseg]
    ; the header
    mov bp, [es:si]                 ; BP = len (safe: no memory refs by BP)
    mov ax, [es:si+2]
    mov [at_caret], ax
    mov ax, [es:si+4]
    mov [at_sela], ax
    mov ax, [es:si+6]
    mov [at_selb], ax
    add si, 8
    ; the text: snapshot ES:SI -> the document at [at_dseg]:0000
    mov ax, es                      ; AX = source segment
    mov cx, bp
    xor di, di
    push ds
    mov es, [at_dseg]               ; ES = the document
    mov ds, ax                      ; DS = the snapshot arena
    cld
    rep movsb
    pop ds
    ; install: text = [0..len), gap = [len..cap)
    mov [at_gs], bp
    mov ax, [at_dcap]
    mov [at_ge], ax
    ; clamp what the header claimed to the length
    mov cx, bp
    mov ax, [at_caret]
    cmp ax, cx
    jbe .c1
    mov [at_caret], cx
.c1:
    mov ax, [at_sela]
    cmp ax, cx
    jbe .c2
    mov [at_sela], cx
.c2:
    mov ax, [at_selb]
    cmp ax, cx
    jbe .c3
    mov [at_selb], cx
.c3:
    mov byte [at_dirty], 1
    mov byte [at_typrun], 0
    call at_layout
    call at_cline_span
    call at_maxtop
    mov bx, ax
    mov ax, [at_top]
    cmp ax, bx
    jbe .t
    mov [at_top], bx
.t:
    call at_caret_off
    call at_draw_text
    call at_sbar
    call at_seecaret
    call at_caret_on
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
; at_match - CF=0 when the CX bytes at logical AX equal the literal at SI
; in:  AX = logical pos, SI = literal, CX = length
; out: CF; preserves all registers (a range past the end is CF=1)
; -----------------------------------------------------------------------------
at_match:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, ax                      ; BX = logical cursor
    mov dx, ax
    add dx, cx                      ; DX = range end
    push cx
    call at_dlen
    pop cx
    cmp dx, ax
    ja .no
.cmp:
    jcxz .yes
    call at_getb
    cmp al, [si]
    jne .no
    inc bx
    inc si
    dec cx
    jmp short .cmp
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
