; =============================================================================
; os8088 - apps/notepad/notepad.asm
;
; NOTEPAD, the third software package (SPEC.md 27) - formerly the built-in
; Note Pad app (KIND_NOTE). Moved out of the kernel to reclaim the 1,317
; bytes it cost there: 281 of code and 1,036 of .bss, nearly all of the
; latter a fixed two-instance text pool. As a package that pool disappears
; entirely - every instance is its own copy in its own segment with its own
; bss (SPEC.md 20.1), so the buffer below is simply per-instance, and the
; instance count is bounded by the arena instead of a hard-coded 2.
;
; Editing behaviour is unchanged from the built-in (SPEC.md 14): printable
; 32..126 append, backspace deletes, Enter stores a newline byte; text wraps
; at the content width with 6px margins, rows that would overflow the
; content bottom are dropped rather than scrolled, and a 1px caret follows
; the text when its own row fits.
;
; What is new is SPEC.md 27.1: F2 saves the note to NOTES.TXT on the mounted
; data disk and F3 loads it back, over the file API of SPEC.md 18.4 - which
; makes this package the first caller of those slots and the proof that they
; work from an ordinary window callback. Line endings are translated in both
; directions (13 here, CR LF on the disk), because the whole point of
; writing a DOS filesystem is that the other machine can read the file.
; Results are reported as a toast in the content's top-right corner, retired
; by the next keystroke.
;
; Those two commands now also have a face. SPEC.md 12.2 gives every
; application the menu bar while its window is frontmost, so Note Pad ships
; a one-menu set - File: New, Open, Save - registered from the entry proc
; and dispatched to np_oncmd. The menu is strictly a second door onto the
; existing routines: "Open" is np_load, "Save" is np_save, F3 and F2 still
; call exactly the same two, and both doors end at np_redraw. "New" is the
; one thing here that is genuinely new rather than a second door - emptying
; the buffer had no key and no button before - and it is menu-only for the
; same reason it was missing: there was no spare key worth spending on it.
;
; The state lookup is the one thing that got simpler. The built-in reached
; its state through inst_of_win -> I_SPTR because all instances shared one
; pool; a package addresses its own bss directly.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'NOTEPAD', np_entry

NP_CAP       equ 512            ; text buffer capacity, bytes
NP_IOCAP     equ NP_CAP * 2     ; staging capacity: every char may become CR LF
NP_BSS_TOTAL equ 570 + NP_IOCAP ; see the bss layout after OS88_IMAGE_END
NP_MARGIN    equ 6              ; left/top text margin inside the content
NP_KEY_SAVE  equ 0x3C           ; F2 scan code (DOS Editor's keys)
NP_KEY_LOAD  equ 0x3D           ; F3
NP_MI_NEW    equ 0              ; File menu item indices - the order of
NP_MI_OPEN   equ 1              ; np_items_file, which is what the kernel
NP_MI_SAVE   equ 2              ; hands np_oncmd in AL (SPEC.md 12.2)
NP_MI_SAVEAS equ 3
NP_NAMEMAX   equ 12             ; 8 + '.' + 3, as SPEC.md 38.6 hands it over

; -----------------------------------------------------------------------------
; np_entry - package entry point (SPEC.md 20.2)
; in:  CS=DS=ES = our own segment, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here. The
; bss arrives zeroed, which is already a fresh empty note - the built-in's
; KD_INIT proc (app_note_kinit) had nothing else to do either.
;
; The menu set is registered here rather than later because the loader's
; wm_show is what draws the first bar (SPEC.md 12.2): by the time the window
; appears, the bar already says "Note Pad  File".
; -----------------------------------------------------------------------------
np_entry:
    push si
    mov si, np_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): np_paint
    call OSAPI_WM_SIZABLE           ; lays out from the live OSAPI_WM_GEOM
    pop ax                          ; answer, so a repaint re-wraps for free
    push si
    mov si, np_menus                ; BX is still the window: hand it our
    call OSAPI_MENU_SET             ; menus (draws nothing, takes no lock)
    pop si                          ; CF is still wm_create's: the branch
                                    ; above consumed it and OSAPI_MENU_SET
                                    ; preserves flags too (SPEC.md 20.3)
    pushf                           ; ...and so must this: the bss arrives
    call np_defname                 ; zeroed and an empty name is not a file
    popf                            ; (SPEC.md 27.1), but np_defname is an
                                    ; ordinary routine and the CF we owe the
                                    ; loader is still riding in the flags
.out:
    retf                            ; far-called by the loader (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; np_paint - W_PAINT: draw the buffer, then the caret
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
np_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = content left, DX = content top
    push dx                         ; the top: wm_geom answers height in DX
    push ax
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    pop ax                          ; AX = content left again
    add cx, ax
    dec cx
    mov [np_rgt], cx                ; content right (inclusive) = left+w-1
    add ax, NP_MARGIN
    mov [np_tx], ax                 ; text origin x = the wrap column
    mov di, ax                      ; DI = pen x
    pop bp                          ; BP = content top
    add dx, bp
    dec dx
    mov [np_bot], dx                ; content bottom (inclusive) = top+h-1
    add bp, NP_MARGIN               ; BP = pen y

    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov bx, [np_len]                ; BX = characters remaining
    mov si, np_buf

.chloop:
    test bx, bx
    jz .caret
    lodsb                           ; DF=0 per SPEC.md 1
    dec bx
    cmp al, 13
    jne .glyph
    mov di, [np_tx]                 ; newline: carriage return + line feed
    add bp, 8
    jmp .chloop

.glyph:
    mov cx, di                      ; wrap if the 8px cell would pass the edge
    add cx, 7
    cmp cx, [np_rgt]
    jbe .fits
    mov di, [np_tx]
    add bp, 8
.fits:
    mov cx, bp                      ; vertical clip: drop rows that overflow,
    add cx, 7                       ; but keep advancing the pen so the caret
    cmp cx, [np_bot]                ; position stays true
    ja .advance
    mov cx, di
    mov dx, bp
    call OSAPI_FONT_CHAR            ; AL still holds the character
.advance:
    add di, 8
    jmp .chloop

.caret:
    mov cx, di                      ; the caret occupies the next cell, so it
    add cx, 7                       ; wraps exactly like a character would
    cmp cx, [np_rgt]
    jbe .cfits
    mov di, [np_tx]
    add bp, 8
.cfits:
    mov cx, bp
    add cx, 7
    cmp cx, [np_bot]
    ja .done                        ; caret row does not fit: no caret
    mov ax, di                      ; 1px black caret, 8 rows tall
    mov bx, bp
    mov dx, bp
    add dx, 7
    call OSAPI_GFX_VLINE

.done:
    pop bp
    pop di
    pop si                      ; SI is the window pointer again
    pop dx
    pop cx
    pop bx
    pop ax
    call np_toast               ; last, so it sits above the text
    retf                        ; far-called W_PAINT (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; np_toast - draw the save/load result over the content's top-right corner
; in:  SI = window ptr, [np_msg] = the string (0 = nothing to say)
; out: nothing; preserves all registers
;
; A framed white box, so it reads over whatever text is under it, clamped to
; the content's left edge on a narrow window. It is drawn from np_paint and
; cleared by the next keystroke, so it can never become stale furniture.
; -----------------------------------------------------------------------------
np_toast:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp word [np_msg], 0
    je .out
    mov bx, si                  ; BX = window ptr
    call OSAPI_WM_CONTENT       ; AX = content left, DX = content top
    mov di, ax                  ; DI = content left (the clamp)
    add dx, 2
    mov [np_by1], dx
    add dx, 11
    mov [np_by2], dx
    call OSAPI_WM_GEOM          ; CX = content width (DX is stored already)
    mov ax, di
    add ax, cx
    sub ax, 3                   ; 2px frame + a 2px gap from the edge
    mov [np_bx2], ax            ; = content left + width - 3 = W_X+W_W-4
    push ax
    mov si, [np_msg]
    call OSAPI_FONT_WIDTH       ; AX = the string's pixel width
    add ax, 7                   ; 4px left pad, 3px right
    mov cx, ax
    pop ax
    sub ax, cx
    cmp ax, di
    jae .xok
    mov ax, di                  ; a narrow window: start at the content left
.xok:
    mov [np_bx1], ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [np_bx1]
    mov bx, [np_by1]
    mov cx, [np_bx2]
    mov dx, [np_by2]
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    mov ax, [np_bx1]
    mov bx, [np_by1]
    mov cx, [np_bx2]
    mov dx, [np_by2]
    call OSAPI_GFX_FRAME
    mov cx, [np_bx1]
    add cx, 4
    mov dx, [np_by1]
    add dx, 2
    mov si, [np_msg]
    call OSAPI_FONT_STR
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_save - write the note to NOTES.TXT (SPEC.md 18.4/27.1)
; in:  nothing (the buffer and its length)
; out: nothing; [np_msg] reports the outcome; preserves all registers
;
; The note's bare 13s become CR LF on the way out, through np_io - which is
; sized for the worst case (every character a newline), so the staging pass
; needs no bounds test of its own.
; -----------------------------------------------------------------------------
np_save:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, np_buf
    mov di, np_io
    mov cx, [np_len]
    xor bx, bx                  ; BX = staged byte count
.stage:
    jcxz .staged
    mov al, [si]
    inc si
    dec cx
    mov [di], al
    inc di
    inc bx
    cmp al, 13
    jne .stage
    mov byte [di], 10           ; the DOS half of the line ending
    inc di
    inc bx
    jmp short .stage
.staged:
    push ds
    pop es                      ; ES:BX = the staged bytes (SPEC.md 20.3)
    mov cx, bx
    mov bx, np_io
    mov si, np_name
    call OSAPI_FILE_WRITE
    jc .err
    mov si, np_m_saved
    call np_setmsg
    jmp short .out
.err:
    call np_errmsg              ; AX = FERR_* -> the toast
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_load - read NOTES.TXT back into the note (SPEC.md 18.4/27.1)
; in:  nothing
; out: nothing; [np_msg] reports the outcome; preserves all registers
;
; CR LF folds back to a single 13, and so does a lone LF - a note written by
; a Unix editor loads correctly too. Anything else outside 32..126 is
; dropped rather than rendered as a stray glyph, and a file longer than the
; buffer fills it and says so.
; -----------------------------------------------------------------------------
np_load:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    mov bx, np_io
    mov cx, NP_IOCAP
    mov si, np_name
    call OSAPI_FILE_READ        ; AX = bytes read
    jc .err
    mov cx, ax
    mov si, np_io
    xor di, di                  ; DI = characters kept
    xor dx, dx                  ; DL = previous byte, DH = truncation flag
.fold:
    jcxz .folded
    mov al, [si]
    inc si
    dec cx
    cmp al, 10
    jne .notlf
    cmp dl, 13
    je .skip                    ; CR LF: the 13 already went in
    mov al, 13                  ; a lone LF is a line break too
.notlf:
    cmp al, 13
    je .store
    cmp al, 32
    jb .skip
    cmp al, 126
    ja .skip
.store:
    cmp di, NP_CAP
    jb .room
    mov dh, 1                   ; the note is full: stop here and say so
    jmp short .folded
.room:
    mov [di+np_buf], al
    inc di
.skip:
    mov dl, al
    jmp short .fold
.folded:
    mov [np_len], di
    mov si, np_m_loaded
    call np_setmsg
    test dh, dh
    jz .out
    mov word [np_msg], np_m_trunc
    jmp short .out
.err:
    call np_errmsg
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_errmsg - turn a FERR_* code into the toast string
; in:  AX = FERR_* (SPEC.md 18.4)
; out: [np_msg]; preserves all registers
; -----------------------------------------------------------------------------
np_errmsg:
    push ax
    push bx
    cmp ax, FERR_BIG
    jbe .known
    mov ax, FERR_IO             ; an unknown code is still a disk problem
.known:
    mov bx, ax
    shl bx, 1
    mov bx, [bx+np_errtab]
    mov [np_msg], bx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_onkey - W_ONKEY: edit the buffer, then repaint our own content
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing; preserves all registers
; Unhandled keys touch nothing; a full buffer drops the keystroke silently.
; -----------------------------------------------------------------------------
np_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    cmp ah, NP_KEY_SAVE
    jne .nosave
    call np_save
    jmp short .redraw
.nosave:
    cmp ah, NP_KEY_LOAD
    jne .noload
    mov al, FDLG_OPEN           ; F3 ASKS now (SPEC.md 27.1): a load with no
    call np_dlgopen             ; way to say what was never the useful half.
    jmp .out                    ; No repaint - the dialog is on top of us
.noload:
    cmp al, 8
    je .bksp
    cmp al, 13
    je .append
    cmp al, 32
    jb .out
    cmp al, 126
    ja .out

.append:
    mov bx, [np_len]
    cmp bx, NP_CAP
    jae .out                        ; full: drop silently
    mov [bx+np_buf], al
    inc word [np_len]
    jmp .edited

.bksp:
    cmp word [np_len], 0
    je .out
    dec word [np_len]

.edited:                        ; an edit retires the toast; an unhandled
    mov word [np_msg], 0        ; key leaves both it and the screen alone

.redraw:
    call np_redraw                  ; SI still = window ptr

.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    retf                            ; far-called W_ONKEY (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; np_redraw - repaint our own content from the buffer
; in:  SI = window ptr (gfx lock held by the caller)
; out: nothing; preserves all registers
;
; The self-repaint every dispatch site shares: white-fill the content, run
; np_paint over it, then put the grow box back, because the fill just erased
; it (SPEC.md 11.1/27). It exists as a routine rather than a tail of
; np_onkey because the menu handler needs exactly the same three steps - the
; kernel does not repaint after a command returns (SPEC.md 12.2), so every
; command that changes the buffer has to end here.
; -----------------------------------------------------------------------------
np_redraw:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = x1, DX = y1
    push dx                         ; y1: wm_geom answers height in DX
    call OSAPI_WM_GEOM              ; CX = content width, DX = content height
    pop bx                          ; BX = y1
    add cx, ax
    dec cx                          ; CX = x2 = x1 + width - 1
    add dx, bx
    dec dx                          ; DX = y2 = y1 + height - 1
    push ax                         ; the pen is a register here, not a
    mov al, CWHITE                  ; variable - keep x1 across the call
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL             ; white-fill the content
    push cs                         ; np_paint returns with retf (it is the
    call np_paint                   ; far-called W_PAINT); SI still = win ptr
    mov bx, si                      ; the white fill erased the grow box;
    call OSAPI_WM_GROW              ; restore it (SPEC.md 11.1/27)
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_new - empty the note (File > New)
; in:  nothing
; out: nothing; preserves all registers
;
; Only the length and the toast are cleared: np_paint reads exactly [np_len]
; bytes, so the stale tail of np_buf is unreachable and wiping 512 bytes
; would buy nothing. The toast goes because "Loaded NOTES.TXT" over an empty
; note is a lie - the same reason an ordinary keystroke retires it.
; -----------------------------------------------------------------------------
np_new:
    mov word [np_len], 0
    mov word [np_msg], 0
    jmp np_defname              ; a new note is a new document: leaving the
                                ; old name would make the next F2 overwrite
                                ; the file the user just walked away from

; -----------------------------------------------------------------------------
; np_oncmd - AM_ONCMD: run a menu command (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0 = File), SI = window ptr,
;      BX = our menu set ptr; gfx lock held by the caller, UI task
; out: nothing; clobbers AX/BX/CX/DX/DI/ES like any window callback
;
; Open and Save are menu-driven twins of F3 and F2 - the same np_load /
; np_save the keyboard path calls, so the two doors can never drift apart;
; New is menu-only, and empties the buffer. Every item changes
; what the window shows - the text, the toast, or both - and the kernel does
; not repaint for us, so all three tails run through np_redraw. The menu
; index is tested even though we register only one menu: the argument is
; kernel input, and an unknown pair must do nothing rather than fall into
; the first case.
; -----------------------------------------------------------------------------
np_oncmd:
    test ah, ah
    jnz .out                        ; not File: nothing of ours
    cmp al, NP_MI_NEW
    je .new
    cmp al, NP_MI_OPEN
    je .open
    cmp al, NP_MI_SAVEAS
    je .saveas
    cmp al, NP_MI_SAVE
    jne .out
    call np_save
    jmp short .draw
.new:
    call np_new
.draw:
    call np_redraw                  ; SI is still the window ptr
.out:
    retf                            ; far-called menu handler (SPEC.md 20.5)
.open:
    mov al, FDLG_OPEN
    jmp short .dlg
.saveas:
    mov al, FDLG_SAVE
.dlg:
    call np_dlgopen                 ; NO repaint after it: the dialog is on
    retf                            ; screen and on top of us, so the usual
                                    ; "commands repaint themselves" tail
                                    ; would draw straight over it. The
                                    ; repaint happens in np_ondlg instead,
                                    ; once it is gone

; -----------------------------------------------------------------------------
; np_dlgopen - raise the Standard File dialog (SPEC.md 38.6)
; in:  AL = FDLG_OPEN or FDLG_SAVE, SI = our window ptr; gfx lock held
; out: nothing; preserves all registers
;
; The current document is handed over as the default, so Save As on a note
; loaded from LETTER.TXT opens with LETTER.TXT already in the box. A refusal
; (CF=1: one is already up) is silently nothing - the dialog the user
; already has IS the answer to the command they just picked.
; -----------------------------------------------------------------------------
np_dlgopen:
    push bx
    push si
    push di
    mov bx, si                      ; the window we want to hear back about
    mov di, np_ondlg
    mov si, np_name
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; np_ondlg - the file dialog's completion callback (SPEC.md 38.6)
; in:  AL = the mode it ran in, SI = our window ptr, ES:DI = the chosen name
;      (ES = KERNEL_SEG - the buffer is the kernel's, read it through ES,
;      never through DS); UI task, gfx lock HELD, the dialog window already
;      destroyed
; out: nothing; no register need be preserved (the kernel saved its own)
;
; One proc for both commands, because the only difference between them is
; which way the bytes move afterwards. It must repaint: the kernel does not
; repaint after a callback returns, and the window under the dialog has just
; been uncovered by wm_destroy.
; -----------------------------------------------------------------------------
np_ondlg:
    mov bl, al                      ; BL = the mode; AL becomes a name byte
    mov dx, si                      ; DX = our window: SI is about to be the
                                    ; kernel's buffer, and np_redraw wants
                                    ; the window back in SI
    mov si, di                      ; ES:SI = the kernel's name buffer
    mov di, np_name
    mov cx, NP_NAMEMAX
.copy:
    mov al, [es:si]                 ; bounded even though SPEC.md 38.6
    mov [di], al                    ; promises <= 12: a package that trusts
    or al, al                       ; a promise is a package with an
    jz .copied                      ; overrun in it
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.copied:
    mov si, dx                      ; SI = our window again
    or bl, bl
    jz .load
    call np_save
    jmp short .draw
.load:
    call np_load
.draw:
    call np_redraw                  ; SI is the window ptr
    retf                            ; far-called completion (SPEC.md 20.5)

; -----------------------------------------------------------------------------
; np_defname - seed the current document name (internal)
; in:  nothing
; out: np_name = 'NOTES.TXT'; preserves all registers
; The loader zeroes our bss (SPEC.md 21 step 5), and an empty name would
; make F2 fail with FERR_NAME on a brand-new note - so a fresh Note Pad
; still has the document the fixed-name version always had.
; -----------------------------------------------------------------------------
np_defname:
    push ax
    push si
    push di
    mov si, np_s_default
    mov di, np_name
.copy:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    or al, al
    jnz .copy
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; np_setmsg - compose a toast around the live document name (internal)
; in:  SI = a NUL prefix ('Saved ' / 'Loaded ')
; out: np_tbuf holds prefix + np_name and [np_msg] points at it; preserves
;      all registers
; -----------------------------------------------------------------------------
np_setmsg:
    push ax
    push si
    push di
    mov di, np_tbuf
.pre:
    mov al, [si]
    or al, al
    jz .name
    mov [di], al
    inc si
    inc di
    jmp short .pre
.name:
    mov si, np_name
.copy:
    mov al, [si]
    mov [di], al
    or al, al
    jz .done
    inc si
    inc di
    jmp short .copy
.done:
    mov word [np_msg], np_tbuf
    pop di
    pop si
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; Same geometry the built-in used: 260x180 outer -> 258x160 content.
np_tpl:
    dw 60, 60, 260, 180
    dw np_ttl, np_paint, np_onkey, 0

np_ttl: db 'Note Pad', 0

; --- the app menu set (SPEC.md 12.2) -------------------------------------------
; One menu, three items, all of them existing behaviour: New empties the
; buffer, Open is F3, Save is F2. AM_NAME reuses np_ttl so the bar label and
; the window title are the same eight characters by construction. The bar
; runs 38 + 64 ('Note Pad') + 16 = 118 to the File cell's left edge, and
; 32 + 12 more to its right edge at 162 - nowhere near the clock at 434.
    OS88_MENUSET np_menus, np_ttl, np_oncmd
        OS88_MENU np_m_file, np_items_file, 4
    OS88_MENUSET_END np_menus

np_m_file:     db 'File', 0
np_items_file: dw np_i_new, np_i_open, np_i_save, np_i_saveas  ; = NP_MI_*
np_i_new:      db 'New', 0
np_i_open:     db 'Open...', 0      ; the ellipsis is the convention and it
np_i_save:     db 'Save', 0         ; is honest: these two ask a question
np_i_saveas:   db 'Save As...', 0   ; first (SPEC.md 38), Save never does

; --- the file and what the toast can say (SPEC.md 27.1) ------------------------
; The name is per-instance state now (np_name in bss), seeded from this at
; launch and replaced by whatever the file dialog returns. The two verbs are
; PREFIXES: np_setmsg composes them with the live name into np_tbuf, because
; a toast that still said NOTES.TXT after a Save As would be worse than no
; toast at all.
np_s_default: db 'NOTES.TXT', 0
np_m_saved:   db 'Saved ', 0
np_m_loaded:  db 'Loaded ', 0
np_m_trunc:   db 'Truncated', 0

; FERR_* (SPEC.md 18.4) -> string, indexed by the code itself
np_errtab:
    dw np_e_ok, np_e_nodisk, np_e_io, np_e_name, np_e_noent, np_e_exist
    dw np_e_full, np_e_dirfull, np_e_prot, np_e_wprot, np_e_big
np_e_ok:      db 'Done', 0
np_e_nodisk:  db 'No disk', 0
np_e_io:      db 'Disk error', 0
np_e_name:    db 'Bad name', 0
np_e_noent:   db 'Not found', 0
np_e_exist:   db 'Name exists', 0
np_e_full:    db 'Disk full', 0
np_e_dirfull: db 'Dir full', 0
np_e_prot:    db 'Protected', 0
np_e_wprot:   db 'Write protected', 0
np_e_big:     db 'Too big', 0

    OS88_BSS NP_BSS_TOTAL
    OS88_IMAGE_END

; --- loader-zeroed bss (SPEC.md 21 step 5) -------------------------------------
; All zero = a fresh empty note with the caret at the origin and no toast.
np_len      equ os88_image_end + 0     ; word: characters used
np_buf      equ os88_image_end + 2     ; NP_CAP bytes of text
np_tx       equ os88_image_end + 514   ; word: paint scratch, text origin x
np_rgt      equ os88_image_end + 516   ; word: content right, inclusive
np_bot      equ os88_image_end + 518   ; word: content bottom, inclusive
np_msg      equ os88_image_end + 520   ; word: toast string, 0 = none
np_bx1      equ os88_image_end + 522   ; word: the toast box, computed in
np_by1      equ os88_image_end + 524   ; np_toast and used by three calls
np_bx2      equ os88_image_end + 526
np_by2      equ os88_image_end + 528
np_name     equ os88_image_end + 530   ; 14: the current document, 8.3 + NUL
                                       ; (SPEC.md 27.1) - per INSTANCE, so
                                       ; two Note Pads hold two documents
np_tbuf     equ os88_image_end + 544   ; 26: 'Saved ' / 'Loaded ' + np_name
np_io       equ os88_image_end + 570   ; NP_IOCAP bytes: the CR LF staging
                                       ; buffer, both directions. Even by
                                       ; construction - both blocks above
                                       ; are even-sized, which is what the
                                       ; old np_pad word was for
                                       ; total 570 + NP_IOCAP = NP_BSS_TOTAL
