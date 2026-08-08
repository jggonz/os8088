; =============================================================================
; os8088 - apps/modplug/mpplist.inc
;
; ModPlug Player's PlayList Editor (SPEC.md 56.8). Prefix mppl_.
;
; ModPlugPlayer's PlayListEditorWindow.ui is a table with Add... / Remove /
; Save List / Load List / Shuffle List / Sort List / Clear List down one side.
; This is that window: a list of queued modules, the same seven buttons, and
; the store behind it is what gives the player's |<< and >>| buttons and its
; Repeat and Shuffle toggles something to mean.
;
; THE STORE IS NOT HERE. [mpp_plist] and its count live in modplug.asm with
; the rest of the player's state, because the TRANSPORT reads them - a song
; ending walks the list whether or not this window was ever opened - and this
; file only edits them. An editor that owned the list would make the playlist
; a property of a window a user may never open.
;
; Two things it does differently from ModPlugPlayer, both on purpose:
;
;   - There is no DOUBLE-CLICK. The kernel delivers one W_ONCLICK per press
;     and a package detecting a double-click would have to compare tick counts
;     itself (SPEC.md 13), which on a 4.77 MHz machine under a 1200-baud
;     serial mouse is a timing guess. Clicking a row SELECTS it; clicking the
;     row that is already selected PLAYS it. No timing, and no gesture that
;     works on a fast machine and not on a slow one.
;   - Save List and Load List are greyed with `(No File)` rather than dropped.
;     A playlist file would be a format this OS has nowhere else, and SPEC.md
;     47's rule 4 wants the reason drawn rather than the control hidden.
;
; Like the Setup window it is UNBOUND (SPEC.md 56.1), so its close and
; minimize boxes are both a plain wm_hide, and it draws with mppui.inc's
; helpers pointed at its own origin.
; =============================================================================

; Two columns of buttons rather than three, for the reason the Setup pane has
; the column widths it has: 'Shuffle List' is twelve glyphs, and a button
; narrow enough for three across cannot hold it. The caption is the control.
MPPL_W      equ 320                 ; content width
MPPL_H      equ 176                 ; ...and height, full layout
MPPL_HC     equ 128                 ; ...compact (CGA)
MPPL_FW     equ MPPL_W + 2
MPPL_ROWH   equ 12                  ; one list row
MPPL_BW     equ 150                 ; one button...
MPPL_BH     equ 16
MPPL_BPX    equ 155                 ; ...and the step to the next
MPPL_BPY    equ 19
MPPL_BCOLS  equ 2
MPPL_BROWS  equ 3
MPPL_NBTN   equ 6

; --- the buttons, in ModPlugPlayer's own order ----------------------------------
mppl_btntab:
    dw mppl_b_add, mppl_b_remove
    dw mppl_b_clear, mppl_b_shuffle
    dw mppl_b_sort, mppl_b_nofile
mppl_btnlab:
    dw mppl_s_add, mppl_s_remove
    dw mppl_s_clear, mppl_s_shuffle
    dw mppl_s_sort, mppl_s_savelist
mppl_s_add:      db 'Add...', 0
mppl_s_remove:   db 'Remove', 0
mppl_s_clear:    db 'Clear List', 0
mppl_s_shuffle:  db 'Shuffle List', 0
mppl_s_sort:     db 'Sort List', 0
mppl_s_savelist: db 'Save List (No File)', 0

; =============================================================================
; mppl_toggle - open the PlayList editor, or put it away again
; in:  gfx lock held; preserves all registers
; =============================================================================
mppl_toggle:
    push ax
    push bx
    push si
    cmp word [mppl_win], 0
    je .make
    call mppl_visible
    or al, al
    jz .show
    mov bx, [mppl_win]
    call OSAPI_WM_HIDE
    jmp .paint
.show:
    mov bx, [mppl_win]
    call OSAPI_WM_FRONT
    call OSAPI_WM_SHOW
    jmp .paint
.make:
    call mppl_geom
    mov si, mppl_tpl
    call OSAPI_WM_CREATE            ; CF=1: the window table is full - a
    jc .paint                       ; refusal, not an error
    mov [mppl_win], bx
    call OSAPI_WM_SHOW
.paint:
    call mppu_refresh              ; the player's PlayList option LED follows
    pop si
    pop bx
    pop ax
    ret

; mppl_visible - AL = 1 while the editor is on screen; preserves everything else
mppl_visible:
    push bx
    push cx
    push dx
    xor al, al
    cmp word [mppl_win], 0
    je .out
    mov bx, [mppl_win]
    call OSAPI_WM_GEOM
    jc .out
    mov al, 1
.out:
    pop dx
    pop cx
    pop bx
    ret

; mppl_geom - place the window and pick its height for THIS screen
mppl_geom:
    push ax
    push bx
    push cx
    push dx
    mov word [mppl_tpl + WT_H], MPPL_H + TITLE_H + 1
    cmp byte [mpp_compact], 0
    je .h
    mov word [mppl_tpl + WT_H], MPPL_HC + TITLE_H + 1
.h:
    mov ax, [mpp_scrw]              ; over to the right of centre, so it does
    sub ax, MPPL_FW                 ; not land squarely on the Setup window
    jns .x
    xor ax, ax
.x:
    shr ax, 1
    add ax, 40
    mov cx, [mpp_scrw]
    sub cx, MPPL_FW
    jns .clamp
    xor cx, cx
.clamp:
    cmp ax, cx
    jbe .xok
    mov ax, cx
.xok:
    mov [mppl_tpl + WT_X], ax
    mov ax, [mpp_docky]
    sub ax, MBAR_H
    sub ax, [mppl_tpl + WT_H]
    jns .y
    xor ax, ax
.y:
    shr ax, 1
    add ax, MBAR_H + 8
    mov [mppl_tpl + WT_Y], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Painting
; =============================================================================
mppl_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call mppl_geom_live
    call mppl_draw_all
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppl_geom_live - latch the size, point the shared helpers at our content,
; and derive how many rows this window has room for
mppl_geom_live:
    push ax
    push bx
    push cx
    push dx
    mov bx, [mppl_win]
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    call mppu_org
    mov bx, [mppl_win]
    call OSAPI_WM_GEOM              ; CX = content w, DX = content h
    mov [mppl_w], cx
    mov [mppl_h], dx
    mov ax, dx                      ; the list ends where the buttons begin
    sub ax, MPPL_BPY * MPPL_BROWS + 8
    mov [mppl_ly2], ax
    sub ax, 6                       ; ...and the rows that fit between
    xor dx, dx
    mov cx, MPPL_ROWH
    div cx
    cmp ax, MPP_PLMAX
    jbe .r
    mov ax, MPP_PLMAX
.r:
    or ax, ax
    jnz .ok
    inc ax                          ; a window too short for a row still shows
.ok:                                ; one, rather than dividing the list by
    mov [mppl_rows], ax             ; zero further down
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppl_redraw - repaint from outside a W_PAINT, and hand the shared drawing
; origin back to the player before anything else uses it
mppl_redraw:
    push ax
    cmp word [mppl_win], 0
    je .out
    call mppl_visible
    or al, al
    jz .out
    call mppl_geom_live
    call mppl_draw_all
    call mppu_org_player
.out:
    pop ax
    ret

mppl_draw_all:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    MPPU_PEN [mppu_body]
    xor ax, ax
    xor bx, bx
    mov cx, [mppl_w]
    dec cx
    mov dx, [mppl_h]
    dec dx
    call mppu_fillr
    call mppl_scroll_fix
    call mppl_draw_list
    call mppl_draw_buttons
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mppl_scroll_fix - keep the selected row inside the visible window
; preserves all registers. Called before every paint rather than at every
; place the selection can move, which is four of them and counting.
; -----------------------------------------------------------------------------
mppl_scroll_fix:
    push ax
    push bx
    mov al, [mpp_plsel]
    cmp al, [mpp_pltop]
    jae .below
    mov [mpp_pltop], al             ; the selection went off the top
    jmp .out
.below:
    mov bl, [mpp_pltop]
    xor bh, bh
    add bx, [mppl_rows]
    cmp al, bl
    jb .out
    sub al, bl
    inc al
    add [mpp_pltop], al             ; ...or off the bottom
.out:
    pop bx
    pop ax
    ret

mppl_draw_list:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, 5                       ; the list's well
    mov bx, 4
    mov cx, [mppl_w]
    sub cx, 6
    mov dx, [mppl_ly2]
    call mppu_well
    cmp byte [mpp_plcount], 0
    jne .rows
    mov si, mppl_s_empty            ; an empty list says what to do about it
    mov cx, 14
    mov dx, 14
    mov al, CWHITE
    mov ah, CBLACK
    call mppu_run
    jmp .out
.rows:
    xor si, si                      ; SI = the visible row, 0..rows-1
.r:
    mov ax, si
    add al, [mpp_pltop]
    cmp al, [mpp_plcount]
    jae .out                        ; past the end of the list
    mov [mppl_ent], ax
    call mppl_row_rect              ; -> AX BX CX DX
    mov di, [mppl_ent]
    cmp di, MPP_PLMAX
    jae .out
    mov byte [mppl_gnd], CBLACK     ; the selection INVERTS rather than taking
    mov byte [mppl_ink], CWHITE     ; a colour: CBLUE reduces to black at 1bpp
    mov al, [mpp_plsel]             ; and would vanish into this black well
    xor ah, ah                      ; (SPEC.md 39.4)
    cmp ax, [mppl_ent]
    jne .txt
    mov byte [mppl_gnd], CWHITE
    mov byte [mppl_ink], CBLACK
    MPPU_PEN CWHITE
    call mppu_fillr
.txt:
    push si
    mov ax, [mppl_ent]
    mov cl, MPP_PLENT
    mul cl                          ; the entry's 13 bytes
    mov si, mpp_plist
    add si, ax
    mov cx, 10
    mov dx, bx
    add dx, 2
    mov al, [mppl_ink]
    mov ah, [mppl_gnd]
    call mppu_run
    pop si
    mov ax, [mppl_ent]              ; the one that is PLAYING gets a marker,
    cmp al, [mpp_plcur]             ; which is a different thing from the one
    jne .next                       ; that is selected
    cmp byte [mpm_loaded], 0
    je .next
    push si
    mov si, mppl_s_playmark
    mov cx, [mppl_w]
    sub cx, 20
    mov dx, bx
    add dx, 2
    mov al, [mppl_ink]
    mov ah, [mppl_gnd]
    call mppu_run
    pop si
.next:
    inc si
    cmp si, [mppl_rows]
    jb .r
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppl_row_rect - SI = the visible row -> AX BX CX DX, its band in the well
mppl_row_rect:
    push si
    mov ax, MPPL_ROWH
    mul si
    add ax, 6
    mov bx, ax
    mov dx, bx
    add dx, MPPL_ROWH - 1
    mov ax, 7
    mov cx, [mppl_w]
    sub cx, 8
    pop si
    ret

mppl_draw_buttons:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor si, si
.b:
    call mppl_btn_rect              ; -> AX BX CX DX
    call mppu_face
    push ax
    push bx
    mov ax, si
    call mppl_btn_live              ; CF=1: this button cannot act right now
    pop bx
    pop ax
    call mppu_dink                  ; greyed WHOLE, per SPEC.md 47 - the face
    mov [mppl_ink], al              ; keeps its bevel so it still reads as a
                                    ; button, and the caption carries the why.
                                    ; The pen, not just the colour: at 1bpp the
                                    ; [gfx_dis] flag is the whole difference
    push si
    mov ax, si
    shl ax, 1
    mov di, ax
    mov si, [mppl_btnlab + di]
    mov cx, [mppl_bx]
    add cx, 5
    mov dx, [mppl_by]
    add dx, 4
    mov al, [mppl_ink]
    mov ah, [mppu_body]
    call mppu_run
    pop si
    inc si
    cmp si, MPPL_NBTN
    jb .b
    call mppu_dpen_off              ; the pen never outlives the routine that
    pop di                          ; set it, whichever branch each button took
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppl_btn_rect - SI = index -> AX BX CX DX, and its origin in [mppl_bx/by]
mppl_btn_rect:
    push si
    push di
    mov ax, si
    xor dx, dx
    mov cx, MPPL_BCOLS
    div cx                          ; AX = the row, DX = the column
    mov di, dx
    mov cx, MPPL_BPY
    mul cx
    add ax, [mppl_h]
    sub ax, MPPL_BPY * MPPL_BROWS + 3
    mov bx, ax                      ; BX = y1
    mov [mppl_by], bx
    mov ax, MPPL_BPX
    mul di
    add ax, 6                       ; AX = x1
    mov [mppl_bx], ax
    mov cx, ax
    add cx, MPPL_BW - 1
    mov dx, bx
    add dx, MPPL_BH - 1
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; mppl_btn_live - AX = a button index; out CF=1 if it cannot act right now
; preserves all registers
;
; The one predicate, read by the drawing and by the click - so a button that
; looks dead cannot act and a button that looks live cannot refuse silently.
; -----------------------------------------------------------------------------
mppl_btn_live:
    cmp ax, 5
    je .dead                        ; Save List: there is no playlist file
    cmp ax, 0
    je .live                        ; Add... always works
    cmp byte [mpp_plcount], 0       ; everything else needs a list to act on
    je .dead
.live:
    clc
    ret
.dead:
    stc
    ret

; =============================================================================
; The buttons themselves
; =============================================================================
; -----------------------------------------------------------------------------
; mppl_b_add - PlayList > Add..., through the Standard File dialog
;
; THE WINDOW PASSED IS THE PLAYER'S, not this editor's, and that is a hard
; requirement rather than a convenience: fdlg_open calls inst_win_owner on it
; and refuses outright when the answer is "nobody" (SPEC.md 38). This window
; is deliberately unbound (SPEC.md 56.1) so that its close box is a plain
; wm_hide - which means it can never be the window a file dialog is opened
; against. The bound player window can, and the completion proc does not care
; which it was told: it reads [mpp_addpl] to know what the answer is for.
;
; [mppl_nodraw] then stops the click handler repainting: the dialog is on top
; now, and nothing in the list has changed yet anyway.
; -----------------------------------------------------------------------------
mppl_b_add:
    push ax
    push bx
    push si
    push di
    mov byte [mpp_addpl], 1
    mov byte [mppl_nodraw], 1
    mov al, FDLG_OPEN
    mov bx, [mpp_win]
    mov di, mpp_fdone
    xor si, si
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    pop ax
    ret

mppl_b_remove:
    push ax
    mov al, [mpp_plsel]
    call mpp_pl_remove
    pop ax
    ret

mppl_b_clear:
    mov byte [mpp_plcount], 0
    mov byte [mpp_plsel], 0
    mov byte [mpp_pltop], 0
    mov byte [mpp_plcur], 0
    call mpp_pl_walk_reset
    ret

mppl_b_shuffle:
    call mpp_pl_shuffle_list
    ret

; -----------------------------------------------------------------------------
; mppl_b_sort - Sort List, by name. An insertion sort over at most 16 entries
; of 13 bytes: 120 comparisons worst case, which on the machine this OS is for
; is still nothing, and it needs no scratch buffer - which a merge would.
; -----------------------------------------------------------------------------
mppl_b_sort:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov cl, [mpp_plcount]
    cmp cl, 2
    jb .out
    xor ch, ch
    mov bx, 1                       ; BX = the entry being placed
.outer:
    cmp bx, cx
    jae .out
    mov ax, bx
.inner:
    or ax, ax
    jz .next
    push ax
    dec ax
    call mpp_pl_ptr                 ; DI = the earlier entry
    mov si, di
    pop ax
    push si
    call mpp_pl_ptr                 ; DI = this one
    pop si
    call mppl_cmp                   ; CF=1 when [SI] > [DI]: they are out of
    jnc .next                       ; order and must swap
    call mppl_swap
    dec ax
    jmp .inner
.next:
    inc bx
    jmp .outer
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; mppl_cmp - DS:SI and DS:DI, two 8.3 names; out CF=1 if [SI] sorts AFTER [DI]
; preserves all registers
mppl_cmp:
    push ax
    push bx
    push cx
    push si
    push di
    mov cx, MPP_PLENT
.c:
    mov al, [si]
    mov bl, [di]
    cmp al, bl
    ja .after
    jb .before
    or al, al                       ; equal all the way to the NUL: not after
    jz .before
    inc si
    inc di
    loop .c
.before:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    clc
    ret
.after:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    stc
    ret

; mppl_swap - exchange the MPP_PLENT bytes at DS:SI and DS:DI
mppl_swap:
    push ax
    push bx
    push cx
    push si
    push di
    mov cx, MPP_PLENT
.s:
    mov al, [si]
    mov bl, [di]
    mov [si], bl
    mov [di], al
    inc si
    inc di
    loop .s
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

mppl_b_nofile:
    ret                             ; greyed, and a refused click on a greyed
                                    ; control says nothing more (47 rule 6)

; =============================================================================
; mppl_onclick - W_ONCLICK for the editor
; in:  CX = x, DX = y (absolute), SI = its window ptr; gfx lock held
; =============================================================================
mppl_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov [mppl_hx], cx               ; bank the click first: the very next call
    mov [mppl_hy], dx               ; answers in DX
    call mppl_geom_live
    mov ax, [mppu_dx0]
    sub [mppl_hx], ax
    mov ax, [mppu_dy0]
    sub [mppl_hy], ax
    ; --- a button ------------------------------------------------------------
    xor si, si
.b:
    call mppl_btn_rect              ; AX BX CX DX
    mov di, [mppl_hx]
    cmp di, ax
    jb .bnext
    cmp di, cx
    ja .bnext
    mov di, [mppl_hy]
    cmp di, bx
    jb .bnext
    cmp di, dx
    ja .bnext
    mov ax, si
    call mppl_btn_live
    jc .done                        ; greyed: refuse in silence
    mov ax, si
    shl ax, 1
    mov di, ax
    mov byte [mppl_nodraw], 0
    call [mppl_btntab + di]
    cmp byte [mppl_nodraw], 0       ; Add... put a dialog on top of us
    jne .done
    jmp .redraw
.bnext:
    inc si
    cmp si, MPPL_NBTN
    jb .b
    ; --- a row ---------------------------------------------------------------
    mov ax, [mppl_hy]
    sub ax, 6
    js .done
    cmp ax, [mppl_ly2]
    jae .done
    xor dx, dx
    mov cx, MPPL_ROWH
    div cx
    cmp ax, [mppl_rows]
    jae .done
    add al, [mpp_pltop]
    cmp al, [mpp_plcount]
    jae .done
    cmp al, [mpp_plsel]             ; clicking the row that is ALREADY
    je .play                        ; selected plays it - the double-click
    mov [mpp_plsel], al             ; that this OS cannot reliably detect
    jmp .redraw
.play:
    call mpp_pl_walk_reset          ; a deliberate start begins a fresh pass
    call mpp_pl_play
.redraw:
    call mppl_geom_live             ; a load may have moved everything
    call mppl_draw_all
    call mppu_org_player
    call mppu_refresh
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; mppl_onkey - Esc closes; the arrows walk the list; Enter plays the selection;
;              anything else is the player's, so one keyboard drives the app
; =============================================================================
mppl_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, ax
    or bl, bl                       ; the SPEC.md 44.2 keypad gate
    jnz .ascii
    cmp bh, 0x48
    je .up
    cmp bh, 0x50
    je .down
    jmp .done
.up:
    cmp byte [mpp_plsel], 0
    je .done
    dec byte [mpp_plsel]
    jmp .redraw
.down:
    mov al, [mpp_plsel]
    inc al
    cmp al, [mpp_plcount]
    jae .done
    mov [mpp_plsel], al
    jmp .redraw
.ascii:
    cmp bl, 27
    je .close
    cmp bl, 13
    je .play
    mov ax, bx
    call mpp_onkey
    jmp .done
.close:
    mov bx, [mppl_win]
    call OSAPI_WM_HIDE
    call mppu_refresh
    jmp .done
.play:
    cmp byte [mpp_plcount], 0
    je .done
    call mpp_pl_walk_reset
    mov al, [mpp_plsel]
    call mpp_pl_play
    call mppu_refresh
.redraw:
    call mppl_geom_live
    call mppl_draw_all
    call mppu_org_player
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Data
; =============================================================================
mppl_tpl:
    dw 0, 0, MPPL_FW, MPPL_H + TITLE_H + 1      ; x/y/h set by mppl_geom
    dw mppl_ttl, mppl_paint, mppl_onkey, mppl_onclick
mppl_ttl:        db 'PlayList Editor', 0
mppl_s_empty:    db 'Empty - Add... queues a module', 0
mppl_s_playmark: db '>', 0

; =============================================================================
; mpplist bss
; =============================================================================
    MPPW mppl_win                   ; 0 until the editor is opened once
    MPPW mppl_w                     ; its content size, latched at paint
    MPPW mppl_h
    MPPW mppl_ly2                   ; where the list ends and the buttons start
    MPPW mppl_rows                  ; ...and how many rows fit above them
    MPPW mppl_hx                    ; a click, banked content-relative
    MPPW mppl_hy
    MPPW mppl_ent                   ; the list entry being drawn
    MPPW mppl_bx                    ; the button being drawn
    MPPW mppl_by
    MPPB mppl_gnd                   ; a row's ground and ink, banked: AX
    MPPB mppl_ink                   ; carries the rect, so AH cannot
    MPPB mppl_nodraw                ; the click opened a dialog - do not
                                    ; repaint over it
