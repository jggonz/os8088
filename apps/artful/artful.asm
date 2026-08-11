; =============================================================================
; os8088 - apps/artful/artful.asm
;
; ARTFULTYPE, the eleventh software package (SPEC.md 46): a port of
; ActionRetro's ArtfulType - "a distraction-free Markdown writing app for
; classic 68k Macintosh computers" (github.com/ActionRetro) - onto os8088's
; fullscreen surface (SPEC.md 11.2). Windowed, the instance is the splash
; card (New / Open, the branding, the 128x100 typewriter); a button or key
; takes the whole screen, where the app draws its own Macintosh menu bar -
; inverted to white-on-black in Writer mode, main.c's UpdateMenuBarLook -
; and edits markdown with live styled rendering, word wrap, drag selection,
; multi-level undo/redo, an app-local clipboard, two zoom sizes, and
; Open/Save through the Standard File dialog. Esc (or File > Quit) hands
; the screen back; the document survives windowed and re-enters intact.
;
; The port's architecture notes live at the top of each include; the
; performance story (a keystroke = one gap store + one paragraph relayout +
; one line blit, sized for a 4.77MHz 8088) is SPEC.md 46.1.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'ArtfulType', at_entry, 1

    OS88_ICON16
    ; a fountain-pen nib on a page - drawn for this port (the original's
    ; icon is a reserved asset; ASSETS_LICENSE)
    dw 0000000110000000b             ; mask: page silhouette + nib
    dw 0000001111000000b
    dw 0000011111100000b
    dw 0000011111100000b
    dw 0000111111110000b
    dw 0000111111110000b
    dw 0001111111111000b
    dw 0001111111111000b
    dw 0011111111111100b
    dw 0011111111111100b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0111111111111110b
    dw 0011111111111100b
    dw 0001111111111000b
    dw 0000011111100000b
    dw 0000000110000000b             ; data: the nib's ink lines
    dw 0000001111000000b
    dw 0000011001100000b
    dw 0000010110100000b
    dw 0000101111010000b
    dw 0000101111010000b
    dw 0001011111101000b
    dw 0001011111101000b
    dw 0010111111110100b
    dw 0010111001110100b
    dw 0101110110111010b
    dw 0101101111011010b
    dw 0101101111011010b
    dw 0010110110110100b
    dw 0001011001101000b
    dw 0000010110100000b
    OS88_ICON16_END

; --- the document lives in the HEAP, not in this package's bss (SPEC.md 46.9)
; It used to be AT_DOCCAP bytes of bss, which was a holdover from a kernel
; where a package got ONE 64KB segment for code and data and there was no
; memory API to ask for anything else. On this kernel a package's REGION is
; itself a heap claim (SPEC.md 20.1), so "works with an empty heap" was never
; a property this app had: if the heap could not fund a claim it could not
; have loaded the app either. What the bss buffer actually cost was 20KB of a
; 60KB region that image + bss share.
AT_KB0     equ 4                    ; the document claim at launch, KB
AT_GROWKB  equ 4                    ; ...and the quantum it grows by
AT_MAXKB   equ 60                   ; ...and its ceiling. NOT the heap's - it
                                    ; is the gap buffer's own arithmetic:
                                    ; at_gs, at_ge, at_caret and every entry
                                    ; in at_lstart are WORDS, so a document
                                    ; can never reach 65,536 bytes whatever
                                    ; the machine has. 60KB leaves the top of
                                    ; that range alone
AT_NL      equ 10                   ; the internal newline
AT_MAXLN   equ 2048                 ; visual-line table entries
AT_LBUFCAP equ 128                  ; one visual line's slice cap
AT_STGCAP  equ 64                   ; relayout staging entries
AT_S1ST    equ 80                   ; 1bpp strip stride (640 px covers the
                                    ; widest text region, Hercules' 592)
AT_S4ST    equ 304                  ; 4bpp strip stride (608 px)
AT_SROWS   equ 30                   ; strip rows (the tallest row height)
AT_CBGCAP  equ 80                   ; per-8px-column background flags
AT_UMAX    equ 15                   ; undo/redo depth (MAX_UNDO_LEVELS)
AT_CLIPBSS equ 2048                 ; clipboard fallback when no claim
AT_FONTBYTES equ 95*8               ; room for the kernel's glyph table
                                    ; (FONT_FIRST..FONT_LAST at 8 rows), which
                                    ; at_font_init copies in and at_glyph
                                    ; indexes as (char-32)*8
AT_ZMAX    equ 1                    ; zoom levels 0..1 (Default / Large)
AT_NMENUS  equ 5

; -----------------------------------------------------------------------------
; at_entry - package entry (SPEC.md 20.2): create the window, own the bar
; -----------------------------------------------------------------------------
at_entry:
    push ds                         ; ES arrives = KERNEL_SEG (SPEC.md 20.2);
    pop es                          ; the string ops below want ES = ours
    mov ax, AT_KB0                  ; the document, before anything else: an
    call OSAPI_MEM_CLAIM            ; editor with nowhere to type is not a
    jc .out                         ; window worth opening, so a refusal is
    mov [at_dseg], dx               ; LD_EABORT and the loader says so - and
    mov word [at_dcap], AT_KB0*1024 ; nothing below has to test [at_dseg]
    push si
    mov si, at_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out
    mov [at_win], bx
    push si
    mov si, at_kmenus
    call OSAPI_MENU_SET             ; the WINDOWED bar (flags preserved)
    pop si
    call at_geom_init               ; live screen numbers (any context)
    call at_menu_init               ; the fullscreen bar's cell widths
    call at_font_init               ; the KERNEL's 8x8 glyphs, copied in
    mov byte [at_writer], 1         ; Writer is the default view (main.c)
    call at_cmd_newdoc              ; a fresh untitled document
    call at_arg                     ; ...unless we were launched to open one
    clc                             ; the CF the loader is owed
.out:
    ret

; -----------------------------------------------------------------------------
; at_font_init - copy the KERNEL's 8x8 glyphs into at_fontbuf
;
; This used to be font_init's probe re-run inside the package - int 10h
; AX=1130h BH=3 with the kernel's own F000:FA6E fallback behind it - which
; paint.asm had already stopped doing (SPEC.md 42) and this one was simply
; missed. OSAPI_FONT_GLYPHS hands the table over, and what that buys is not
; the forty lines: it is that this app letters in the SAME TYPEFACE as the UI
; around it. On a `make FONT=` kernel (SPEC.md 6.2) the two are different
; fonts, so a probe here would have rendered the document in the machine's ROM
; face inside a window drawn in ours.
;
; The local copy stays. Paint fetches eight bytes at a time through the
; answered segment because 760 bytes is 3.8% of everything it is allowed to
; be; here at_glyph is the inner loop of a styled, scaled renderer and reads
; [si] through DS, so a copy keeps that loop untouched.
; -----------------------------------------------------------------------------
at_font_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    call OSAPI_FONT_GLYPHS          ; DX:SI = the table, AL..AH = the range it
                                    ; covers, CX = bytes per glyph
    mov bx, dx                      ; bank the SEGMENT: mul below eats DX
    sub ah, al
    mov al, ah
    xor ah, ah
    inc ax                          ; AX = glyphs the kernel actually has...
    mul cx                          ; ...times its own bytes-per-glyph, rather
    cmp ax, AT_FONTBYTES            ; than 95*8 assumed from here
    jbe .fits
    mov ax, AT_FONTBYTES            ; a bigger table than we reserved for is
.fits:                              ; truncated, never written past
    mov cx, ax
    shr cx, 1
    mov di, at_fontbuf
    push ds
    pop es                          ; ES = ours: the destination
    mov ds, bx                      ; DS = the table's own segment, which is
    cld                             ; LOW_SEG and NOT KERNEL_SEG (SPEC.md 6)
    rep movsw
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_menu_init - measure the fullscreen menus' pull-down widths
; -----------------------------------------------------------------------------
at_menu_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor di, di                      ; DI = menu index
.menu:
    cmp di, AT_NMENUS
    jae .done
    mov bx, di
    add bx, bx
    add bx, di
    add bx, bx                      ; BX = di*6
    mov cx, [bx+at_mtab+4]          ; CX = item count
    mov si, [bx+at_mtab+2]          ; SI = item array
    xor dx, dx                      ; DX = the running max width
.item:
    jcxz .store
    mov ax, [si]
    or ax, ax
    jz .sep
    push si
    mov si, ax
    call OSAPI_FONT_WIDTH
    mov bx, ax                      ; BX = label px
    pop si
    mov ax, [si+2]
    or ax, ax
    jz .width
    push si
    push bx
    mov si, ax
    call OSAPI_FONT_WIDTH
    mov bx, ax
    pop ax                          ; AX = label px back
    add bx, ax
    add bx, 10                      ; the gap between label and shortcut
    pop si
    xchg bx, ax
    mov bx, ax                      ; (settle: BX = combined px)
.width:
    add bx, 14+6                    ; check gutter + right inset
    cmp bx, dx
    jbe .sep
    mov dx, bx
.sep:
    add si, 6
    dec cx
    jmp short .item
.store:
    mov bx, di
    shl bx, 1
    mov [bx+at_mwid], dx
    inc di
    jmp short .menu
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_paint - W_PAINT (content freshly white-filled, gfx lock held)
; in:  SI = window ptr
; -----------------------------------------------------------------------------
at_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    cmp byte [at_argp], 0           ; a document handed to us at launch
    je .noarg                       ; (SPEC.md 54.5): the gfx lock is held
    call at_argload                 ; here and the window is placed, which
    jc .done                        ; the entry proc could promise neither.
.noarg:                             ; CF=1: it entered the editor and painted
    push ds                         ; ES arrives = KERNEL_SEG (SPEC.md 20.2)
    pop es
    mov byte [at_cshown], 0         ; fresh pixels carry no caret
    mov byte [at_cphase], 0
    cmp byte [at_fs], 0
    je .win
    call at_fs_paint_body
    jmp short .done
.win:
    call at_splash
.done:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_fs_paint_body - bar + text + scroll bar (+ a live alert on top)
at_fs_paint_body:
    call at_bar_draw
    call at_draw_text
    call at_sbar
    cmp byte [at_modal], 0
    je .out
    push ax
    push si
    mov al, [at_modal]              ; a wm_paint_all crossed a live alert:
    mov si, [at_mmsg]               ; put it back on top
    mov byte [at_modal], 0          ; (at_alert re-latches it)
    call at_alert
    pop si
    pop ax
.out:
    ret

; at_fs_paint_all - self-initiated full repaint (screen not pre-filled)
at_fs_paint_all:
    push ax
    push bx
    push cx
    push dx
    mov byte [at_cshown], 0
    mov al, CWHITE
    call OSAPI_SET_COLOR
    xor ax, ax
    xor bx, bx
    mov cx, [at_vw]
    dec cx
    mov dx, [at_vh]
    dec dx
    call OSAPI_GFX_FILL
    call at_fs_paint_body
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_onkey - W_ONKEY (AL = ascii, AH = scan, SI = window ptr; lock held)
; -----------------------------------------------------------------------------
at_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds                         ; ES arrives = KERNEL_SEG (SPEC.md 20.2)
    pop es
    cmp byte [at_fs], 0
    je .win
    cmp byte [at_modal], 0
    je .ed
    call at_modal_key
    jmp short .done
.ed:
    call at_ed_key
    jmp short .done
.win:
    ; the splash listens for its two verbs
    cmp al, 13
    je .new
    cmp al, 'n'
    je .new
    cmp al, 'N'
    je .new
    cmp al, 'o'
    je .open
    cmp al, 'O'
    je .open
    cmp al, 'w'                     ; Write: back into the editor with the
    je .write                       ; document as it stands
    cmp al, 'W'
    je .write
    jmp short .done
.write:
    call at_fs_enter
    jmp short .done
.new:
    call at_cmd_newdoc
    call at_fs_enter
    jmp short .done
.open:
    call at_dlg_open
.done:
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
; at_onclick - W_ONCLICK (CX = x, DX = y, absolute screen; lock held)
; -----------------------------------------------------------------------------
at_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ds                         ; ES arrives = KERNEL_SEG (SPEC.md 20.2)
    pop es
    cmp byte [at_fs], 0
    je .win
    cmp byte [at_modal], 0
    jne .modal
    cmp dx, 20
    jb .bar
    mov ax, [at_sbx]
    cmp cx, ax
    jb .text
    add ax, 16
    cmp cx, ax
    jae .text
    call at_sb_click
    jmp short .done
.text:
    call at_click_text
    jmp short .done
.bar:
    call at_bar_click
    jmp short .done
.modal:
    call at_modal_click
    jmp short .done
.win:
    call at_splash_click
.done:
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
; at_koncmd - the WINDOWED bar's menu handler (SPEC.md 12.2)
; in:  AL = item, AH = menu (0 = File), SI = window ptr; lock held
; -----------------------------------------------------------------------------
at_koncmd:
    push ds                         ; ES arrives = KERNEL_SEG (SPEC.md 20.2)
    pop es
    or ah, ah
    jnz .out
    cmp al, 0
    je .new
    cmp al, 1
    je .open
    cmp al, 2
    jne .out
    call at_fs_enter                ; Write: the current document, fullscreen
    jmp short .out
.new:
    call at_cmd_newdoc
    call at_fs_enter
    jmp short .out
.open:
    call at_dlg_open
.out:
    ret

; -----------------------------------------------------------------------------
; at_fs_enter / at_fs_exit - the fullscreen transitions (SPEC.md 11.2,
; tracker's idiom: the flag flips BEFORE the call, because the W_PAINT that
; runs inside wm_front must already see the fullscreen answer)
; -----------------------------------------------------------------------------
at_fs_enter:
    push ax
    push bx
    cmp byte [at_fs], 0
    jne .out
    call at_geom_init               ; the live screen, fresh every entry
    mov byte [at_fs], 1
    call at_layout                  ; the text width just became real
    call at_cline_span
    call at_maxtop
    mov bx, ax
    mov ax, [at_top]
    cmp ax, bx
    jbe .t
    mov [at_top], bx
.t:
    mov al, 1
    mov bx, [at_win]
    call OSAPI_FULLSCREEN           ; fronts + repaints under the held lock
    jnc .ok
    mov byte [at_fs], 0             ; refused: another window owns the
    jmp short .out                  ; screen; the splash stays
.ok:
    call at_undo_init               ; the heap claim, once
    cmp byte [at_wspawned], 0
    jne .out
    mov ax, at_worker
    mov bx, [at_win]
    call OSAPI_TASK_SPAWN           ; the caret-blink worker
    jc .out                         ; transient refusal: retry next entry
    mov byte [at_wspawned], 1
.out:
    pop bx
    pop ax
    ret

at_fs_exit:
    push ax
    push bx
    cmp byte [at_fs], 0
    je .out
    mov byte [at_fs], 0
    mov byte [at_cshown], 0
    mov al, 0
    mov bx, [at_win]
    call OSAPI_FULLSCREEN           ; restores geometry + wm_paint_all,
.out:                               ; which re-enters at_paint windowed
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_worker - the caret-blink task (SPEC.md 20.6; app_bounce_task's rules)
; in:  DX = instance index (unused); never returns - OSAPI_TASK_ALIVE is
;      where this task dies when the window closes
; -----------------------------------------------------------------------------
at_worker:
    push cs                         ; ES is unspecified at task birth; the
    pop es                          ; caret path wants ES = ours (CS = DS)
.loop:
    mov bx, [at_win]
    call OSAPI_TASK_ALIVE           ; the close box ends the task in here
    mov ax, 9                       ; ~half a second a phase
    call OSAPI_TASK_SLEEP
    call .gate
    jc .loop
    call OSAPI_GFX_LOCK
    call .gate                      ; re-check UNDER the lock (rule 5)
    jc .unlock
    mov bx, [at_win]
    call OSAPI_WM_GEOM
    jc .unlock                      ; hidden
    call OSAPI_WM_CLIP_SET
    jc .unlock                      ; not a visible pixel
    xor byte [at_cphase], 1
    cmp byte [at_cphase], 0
    jne .dark
    call at_caret_on
    jmp short .unlock
.dark:
    call at_caret_off
.unlock:
    call OSAPI_GFX_UNLOCK
    jmp short .loop
.gate:
    cmp byte [at_fs], 0
    je .no
    cmp byte [at_modal], 0
    jne .no
    cmp byte [at_menuon], 0
    jne .no
    cmp byte [at_drag], 0
    jne .no
    clc
    ret
.no:
    stc
    ret

; the engine
%include "atdoc.inc"
%include "atrend.inc"
%include "atui.inc"
%include "atedit.inc"
%include "atcmd.inc"
%include "atfile.inc"
%include "atimg.inc"

; --- the window template (SPEC.md 11) ------------------------------------------
at_tpl:
    dw 140, 60, 360, 300
    dw at_ttl, at_paint, at_onkey, at_onclick

at_ttl: db 'ArtfulType', 0

; --- the WINDOWED bar's menu set (SPEC.md 12.2) --------------------------------
    OS88_MENUSET at_kmenus, at_ttl, at_koncmd
        OS88_MENU at_km_file, at_km_items, 3
    OS88_MENUSET_END at_kmenus

at_km_file:  db 'File', 0
at_km_items: dw at_ki_new, at_ki_open, at_ki_write
at_ki_new:   db 'New', 0
at_ki_open:  db 'Open...', 0
at_ki_write: db 'Write', 0

; --- the FULLSCREEN menus (at_mtab: title, items, count - 6 bytes each) --------
; item records: dw label (0 = separator), dw shortcut label, db AT_CMD_*,
; db AT_MS_* state selector
at_mtab:
    dw at_m_file,  at_mi_file,  6
    dw at_m_edit,  at_mi_edit,  8
    dw at_m_style, at_mi_style, 12
    dw at_m_view,  at_mi_view,  6
    dw at_m_help,  at_mi_help,  1

at_m_file:  db 'File', 0
at_m_edit:  db 'Edit', 0
at_m_style: db 'Style', 0
at_m_view:  db 'View', 0
at_m_help:  db 'Help', 0

at_mi_file:
    dw at_i_new,      at_k_n
    db AT_CMD_NEW, AT_MS_PLAIN
    dw at_i_open,     at_k_o
    db AT_CMD_OPEN, AT_MS_PLAIN
    dw at_i_save,     at_k_s
    db AT_CMD_SAVE, AT_MS_PLAIN
    dw at_i_saveas,   0
    db AT_CMD_SAVEAS, AT_MS_PLAIN
    dw 0, 0
    db 0, 0
    dw at_i_quit,     at_k_q
    db AT_CMD_QUIT, AT_MS_PLAIN

at_mi_edit:
    dw at_i_undo,     at_k_z
    db AT_CMD_UNDO, AT_MS_UNDO
    dw at_i_redo,     at_k_sz
    db AT_CMD_REDO, AT_MS_REDO
    dw 0, 0
    db 0, 0
    dw at_i_cut,      at_k_x
    db AT_CMD_CUT, AT_MS_PLAIN
    dw at_i_copy,     at_k_c
    db AT_CMD_COPY, AT_MS_PLAIN
    dw at_i_paste,    at_k_v
    db AT_CMD_PASTE, AT_MS_PLAIN
    dw 0, 0
    db 0, 0
    dw at_i_selall,   at_k_a
    db AT_CMD_SELALL, AT_MS_PLAIN

at_mi_style:
    dw at_i_bold,     at_k_b
    db AT_CMD_BOLD, AT_MS_PLAIN
    dw at_i_italic,   at_k_i
    db AT_CMD_ITALIC, AT_MS_PLAIN
    dw at_i_code,     at_k_k
    db AT_CMD_CODE, AT_MS_PLAIN
    dw at_i_strike,   0
    db AT_CMD_STRIKE, AT_MS_PLAIN
    dw 0, 0
    db 0, 0
    dw at_i_h1,       0
    db AT_CMD_H1, AT_MS_PLAIN
    dw at_i_h2,       0
    db AT_CMD_H2, AT_MS_PLAIN
    dw at_i_h3,       0
    db AT_CMD_H3, AT_MS_PLAIN
    dw 0, 0
    db 0, 0
    dw at_i_link,     at_k_l
    db AT_CMD_LINK, AT_MS_PLAIN
    dw 0, 0
    db 0, 0
    dw at_i_none,     0
    db AT_CMD_NONE, AT_MS_PLAIN

at_mi_view:
    dw at_i_mdview,   0
    db AT_CMD_VIEWMD, AT_MS_MDVIEW
    dw at_i_wrview,   0
    db AT_CMD_VIEWWR, AT_MS_WRVIEW
    dw 0, 0
    db 0, 0
    dw at_i_zin,      0
    db AT_CMD_ZIN, AT_MS_ZIN
    dw at_i_zout,     0
    db AT_CMD_ZOUT, AT_MS_ZOUT
    dw at_i_zdef,     0
    db AT_CMD_ZDEF, AT_MS_ZDEF

at_mi_help:
    dw at_i_about,    0
    db AT_CMD_ABOUT, AT_MS_PLAIN

at_i_new:    db 'New', 0
at_i_open:   db 'Open...', 0
at_i_save:   db 'Save', 0
at_i_saveas: db 'Save As...', 0
at_i_quit:   db 'Quit', 0
at_i_undo:   db 'Undo', 0
at_i_redo:   db 'Redo', 0
at_i_cut:    db 'Cut', 0
at_i_copy:   db 'Copy', 0
at_i_paste:  db 'Paste', 0
at_i_selall: db 'Select All', 0
at_i_bold:   db 'Bold', 0
at_i_italic: db 'Italic', 0
at_i_code:   db 'Code', 0
at_i_strike: db 'Strikethrough', 0
at_i_h1:     db 'Heading 1', 0
at_i_h2:     db 'Heading 2', 0
at_i_h3:     db 'Heading 3', 0
at_i_link:   db 'Link', 0
at_i_none:   db 'None', 0
at_i_mdview: db 'Markdown', 0
at_i_wrview: db 'Writer', 0
at_i_zin:    db 'Zoom In', 0
at_i_zout:   db 'Zoom Out', 0
at_i_zdef:   db 'Default Size', 0
at_i_about:  db 'About The Artful Type...', 0

at_k_n:  db '^N', 0
at_k_o:  db '^O', 0
at_k_s:  db '^S', 0
at_k_q:  db '^Q', 0
at_k_z:  db '^Z', 0
at_k_sz: db 'Sh^Z', 0
at_k_x:  db '^X', 0
at_k_c:  db '^C', 0
at_k_v:  db '^V', 0
at_k_a:  db '^A', 0
at_k_b:  db '^B', 0
at_k_i:  db '^I', 0
at_k_k:  db '^K', 0
at_k_l:  db '^L', 0

; --- the alert/splash strings --------------------------------------------------
at_s_savechg:  db 'Save changes to ', 0
at_s_qmark:    db '?', 0
at_s_untitled: db 'Untitled', 0
at_s_save:     db 'Save', 0
at_s_cancel:   db 'Cancel', 0
at_s_dontsave: db "Don't Save", 0
at_s_ok:       db 'OK', 0
at_s_new:      db 'New', 0
at_s_open:     db 'Open...', 0
at_s_title:    db 'The Artful Type', 0
at_s_sub:      db 'A Distraction-Free Writing Environment', 0
at_s_ver:      db 'v0.1 - the os8088 port', 0
at_s_url:      db 'github.com/ActionRetro', 0
at_s_defname:  db 'UNTITLED.MD', 0
at_s_toobig:   db 'The selection is too large to copy.', 0
at_s_nofit:    db 'Not enough room in the document.', 0

; --- geometry tables: [zoom*4 + heading level] ---------------------------------
; level:        0   1   2   3        (H1/H2 double, H3 body-size bold at
at_cellwtab: db 8, 16, 16,  8        ; zoom 0; everything one step up at
             db 16, 24, 24, 16       ; zoom 1)
at_rowhtab:  db 10, 20, 20, 12
             db 20, 30, 30, 22

; --- the pixel-expansion tables ------------------------------------------------
; at_dbl: nibble -> horizontally doubled byte (bit 3 -> bits 7,6 ...)
at_dbl:
%assign n 0
%rep 16
    %assign v 0
    %assign i 0
    %rep 4
        %if n & (1 << i)
            %assign v v | (3 << (2*i))
        %endif
        %assign i i+1
    %endrep
    db v
    %assign n n+1
%endrep

; at_trp: nibble -> tripled 12 bits (bit 3 -> bits 11..9 ...), in a word
at_trp:
%assign n 0
%rep 16
    %assign v 0
    %assign i 0
    %rep 4
        %if n & (1 << i)
            %assign v v | (7 << (3*i))
        %endif
        %assign i i+1
    %endrep
    dw v
    %assign n n+1
%endrep

; at_x4w / at_x4g: a 1bpp byte -> 4 packed-4bpp bytes (8 px, high nibble
; leftmost); set bits ink CBLACK, clear bits CWHITE - or CLGRAY for the
; code-span background (a 50% dither on the 1bpp adapters, SPEC.md 39.4)
%macro AT_X4TAB 1
%assign n 0
%rep 256
    %assign k 0
    %rep 4
        %assign hi ((n >> (7 - 2*k)) & 1)
        %assign lo ((n >> (6 - 2*k)) & 1)
        %if hi
            %assign hv 0
        %else
            %assign hv %1
        %endif
        %if lo
            %assign lv 0
        %else
            %assign lv %1
        %endif
        db (hv << 4) | lv
        %assign k k+1
    %endrep
    %assign n n+1
%endrep
%endmacro

at_x4w: AT_X4TAB 15
at_x4g: AT_X4TAB 7

; =============================================================================
; bss (SPEC.md 20.1: loader-zeroed; all-zero = windowed, markdown-empty,
; caret 0 - at_entry then flips Writer on and seeds the name)
; =============================================================================

AT_BSS_TOTAL equ (at_bss_end - at_bss_base)

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%include "os88ui.inc"

    OS88_BSS AT_BSS_TOTAL
    OS88_IMAGE_END

at_bss_base equ os88_image_end

; --- the document -------------------------------------------------------------
; The TEXT is not here - it is [at_dseg]:0000, a heap claim (SPEC.md 46.9).
; What stays in bss is the bookkeeping, which is fixed-size and tiny.
at_dseg     equ at_bss_base                  ; word: the document claim, and
                                             ; never 0 while this instance
                                             ; lives - at_entry aborts the
                                             ; launch rather than open an
                                             ; editor with nowhere to type
at_dcap     equ at_dseg + 2                  ; word: its capacity in bytes,
                                             ; kept in step by at_dresize
at_dkb      equ at_dcap + 2                  ; word } at_dresize's scratch: the
at_dnew     equ at_dkb + 2                   ; word } size it is moving to, in
at_dtail    equ at_dnew + 2                  ; word } KB and in bytes, and the
                                             ; length of the gap's high run,
                                             ; which has to survive a segment
                                             ; switch that makes no package
                                             ; variable readable
at_gs       equ at_dtail + 2                 ; word: gap start
at_ge       equ at_gs + 2                    ; word: gap end
at_caret    equ at_ge + 2                    ; word
at_sela     equ at_caret + 2                 ; word: selection anchor
at_selb     equ at_sela + 2                  ; word: selection point
at_dirty    equ at_selb + 2                  ; byte
at_writer   equ at_dirty + 1                 ; byte: 1 = Writer mode
at_zoom     equ at_writer + 1                ; byte: 0..AT_ZMAX
at_fs       equ at_zoom + 1                  ; byte: fullscreen latch
at_pad1     equ at_fs + 1                    ; byte
; --- layout -------------------------------------------------------------------
at_lstart   equ at_pad1 + 1                  ; AT_MAXLN words
at_lattr    equ at_lstart + AT_MAXLN*2       ; AT_MAXLN bytes
at_nlines   equ at_lattr + AT_MAXLN          ; word
at_lput     equ at_nlines + 2                ; word: scan write index
at_stgmode  equ at_lput + 2                  ; byte
at_schl     equ at_stgmode + 1               ; byte: scan heading level
at_sccont   equ at_schl + 1                  ; byte: scan continuation flag
at_scspan   equ at_sccont + 1                ; byte: scan span nibble
at_scend    equ at_scspan + 1                ; word
at_scpos    equ at_scend + 2                 ; word
at_scskip   equ at_scpos + 2                 ; word
at_sccw     equ at_scskip + 2                ; word: scan cell width
at_sclst    equ at_sccw + 2                  ; word: scan line start
at_stgs     equ at_sclst + 2                 ; AT_STGCAP words: staging starts
at_stga     equ at_stgs + AT_STGCAP*2        ; AT_STGCAP bytes: staging attrs
at_rldel    equ at_stga + AT_STGCAP          ; word: relayout delta
at_rlst     equ at_rldel + 2                 ; word: relayout scan start
at_rlj      equ at_rlst + 2                  ; word: surviving old index
at_rlk      equ at_rlj + 2                   ; word: staged entry count
at_rlfull   equ at_rlk + 2                   ; byte: fell back to at_layout
at_dfromb   equ at_rlfull + 1                ; byte: pad
at_dfrom    equ at_dfromb + 1                ; word: first changed line
; --- screen geometry ----------------------------------------------------------
at_vw       equ at_dfrom + 2                 ; word
at_vh       equ at_vw + 2                    ; word
at_vbpp     equ at_vh + 2                    ; byte
at_pad2     equ at_vbpp + 1                  ; byte
at_tx0      equ at_pad2 + 1                  ; word: text left
at_ty0      equ at_tx0 + 2                   ; word: text top
at_ty1      equ at_ty0 + 2                   ; word: text bottom (exclusive)
at_tw       equ at_ty1 + 2                   ; word: text width
at_sbx      equ at_tw + 2                    ; word: scroll bar left
at_top      equ at_sbx + 2                   ; word: first visible line
at_goal     equ at_top + 2                   ; word: up/down goal x
; --- the parser's per-line arrays ---------------------------------------------
at_lbuf     equ at_goal + 2                  ; AT_LBUFCAP bytes: the slice
at_vis      equ at_lbuf + AT_LBUFCAP         ; AT_LBUFCAP bytes: 1 = hidden
at_sty      equ at_vis + AT_LBUFCAP          ; AT_LBUFCAP bytes: AT_ST_*
at_xmap     equ at_sty + AT_LBUFCAP          ; AT_LBUFCAP+1 words: x per char
at_cellbg   equ at_xmap + (AT_LBUFCAP+1)*2   ; AT_CBGCAP bytes
at_pcnt     equ at_cellbg + AT_CBGCAP        ; word
at_pstart   equ at_pcnt + 2                  ; word
at_pcw      equ at_pstart + 2                ; word
at_prh      equ at_pcw + 2                   ; word
at_psc      equ at_prh + 2                   ; word
at_pspan    equ at_psc + 2                   ; byte
at_pbase    equ at_pspan + 1                 ; byte
at_plt      equ at_pbase + 1                 ; word: link text end
at_plh      equ at_plt + 2                   ; word: link hide end
at_pforce   equ at_plh + 2                   ; byte: force styled parse
at_gsty     equ at_pforce + 1                ; byte: composing style
at_grow     equ at_gsty + 1                  ; 4 bytes: scaled glyph row
at_cll0     equ at_grow + 4                  ; word: caret paragraph first
at_cll1     equ at_cll0 + 2                  ; word: caret paragraph last
; --- the strips ---------------------------------------------------------------
at_strip1   equ at_cll1 + 2                  ; AT_S1ST * AT_SROWS
at_strip4   equ at_strip1 + AT_S1ST*AT_SROWS ; AT_S4ST * AT_SROWS
at_xw       equ at_strip4 + AT_S4ST*AT_SROWS ; word: expand/blit width
at_dly      equ at_xw + 2                    ; word: draw-line y
; --- the caret ----------------------------------------------------------------
at_cshown   equ at_dly + 2                   ; byte
at_cphase   equ at_cshown + 1                ; byte: 0 lit / 1 dark
at_ccx      equ at_cphase + 1                ; word
at_ccy      equ at_ccx + 2                   ; word
at_ccy2     equ at_ccy + 2                   ; word
; --- ui state -----------------------------------------------------------------
at_modal    equ at_ccy2 + 2                  ; byte: AT_MD_* or 0
at_menuon   equ at_modal + 1                 ; byte
at_drag     equ at_menuon + 1                ; byte
at_shift    equ at_drag + 1                  ; byte
at_typrun   equ at_shift + 1                 ; byte
at_pend     equ at_typrun + 1                ; byte: pending action
at_havefile equ at_pend + 1                  ; byte
at_wspawned equ at_havefile + 1              ; byte
at_mcur     equ at_wspawned + 1              ; word: open menu
at_mhil     equ at_mcur + 2                  ; word: hovered item
at_msx1     equ at_mhil + 2                  ; word: menu/alert scratch
at_msx2     equ at_msx1 + 2                  ; word
at_msy2     equ at_msx2 + 2                  ; word
at_mwid     equ at_msy2 + 2                  ; AT_NMENUS words
at_mmsg     equ at_mwid + AT_NMENUS*2        ; word: alert message
at_b1x      equ at_mmsg + 2                  ; the buttons: x, y, width
at_b1y      equ at_b1x + 2
at_b1w      equ at_b1y + 2
at_b2x      equ at_b1w + 2
at_b2y      equ at_b2x + 2
at_b2w      equ at_b2y + 2
at_b3x      equ at_b2w + 2
at_b3y      equ at_b3x + 2
at_b3w      equ at_b3y + 2
at_spx      equ at_b3w + 2                   ; splash geometry
at_spy      equ at_spx + 2
at_spw      equ at_spy + 2
at_sph      equ at_spw + 2
at_spcx     equ at_sph + 2
at_btx      equ at_spcx + 2                  ; bigtext/image cursor
at_bty      equ at_btx + 2
; --- edit scratch -------------------------------------------------------------
at_ocll0    equ at_bty + 2
at_ocll1    equ at_ocll0 + 2
at_oselb    equ at_ocll1 + 2
at_nvlo     equ at_oselb + 2
at_nvhi     equ at_nvlo + 2
at_xrlo     equ at_nvhi + 2
at_xrhi     equ at_xrlo + 2
at_xry      equ at_xrhi + 2
at_rby      equ at_xry + 2
at_pastepos equ at_rby + 2
at_wlo      equ at_pastepos + 2
at_whi      equ at_wlo + 2
at_wplen    equ at_whi + 2
at_wn       equ at_wplen + 2
at_ferr     equ at_wn + 2
; --- the window, the file, the clipboard, undo --------------------------------
at_win      equ at_ferr + 2                  ; word: our window ptr
at_name     equ at_win + 2                   ; 14 bytes: 8.3 + NUL
at_cliplen  equ at_name + 14                 ; word
at_argp     equ at_cliplen + 2              ; byte: 1 = launched to open
at_argdrv   equ at_argp + 1                 ; byte: ...and where it lives
at_argclus  equ at_argdrv + 1               ; word
at_aseg     equ at_argclus + 2               ; word: heap claim (0 = none)
at_usz      equ at_aseg + 2                  ; word: per-stack bytes
at_coff     equ at_usz + 2                   ; word: clip slice offset
at_csz      equ at_coff + 2                  ; word: clip slice bytes
at_ucnt     equ at_csz + 2                   ; word
at_utop     equ at_ucnt + 2                  ; word
at_rcnt     equ at_utop + 2                  ; word
at_rtop     equ at_rcnt + 2                  ; word
at_uoffs    equ at_rtop + 2                  ; AT_UMAX words
at_roffs    equ at_uoffs + AT_UMAX*2         ; AT_UMAX words
at_snstk    equ at_roffs + AT_UMAX*2         ; byte: staged stack id
at_snpad    equ at_snstk + 1                 ; byte
at_sncnt    equ at_snpad + 1                 ; word
at_sntop    equ at_sncnt + 2                 ; word
at_snbase   equ at_sntop + 2                 ; word
at_snoffs   equ at_snbase + 2                ; word
at_clipbss  equ at_snoffs + 2                ; AT_CLIPBSS bytes
at_fontbuf  equ at_clipbss + AT_CLIPBSS      ; the kernel's glyph table,
                                             ; copied in at launch
at_bss_end  equ at_fontbuf + AT_FONTBYTES
