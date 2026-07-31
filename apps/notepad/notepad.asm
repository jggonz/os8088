; =============================================================================
; os8088 - apps/notepad/notepad.asm
;
; NOTEPAD, the third software package (SPEC.md 27) - formerly the built-in
; Note Pad app (KIND_NOTE). Moved out of the kernel to reclaim the 1,317
; bytes it cost there: 281 of code and 1,036 of .bss, nearly all of the
; latter a fixed two-instance text pool. As a package that pool disappears
; entirely - every instance is its own relocated copy with its own bss
; (SPEC.md 20.1), so the buffer below is simply per-instance, and the
; instance count is bounded by the region pool instead of a hard-coded 2.
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
; The state lookup is the one thing that got simpler. The built-in reached
; its state through inst_of_win -> I_SPTR because all instances shared one
; pool; a package addresses its own bss directly.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'NOTEPAD', np_entry

NP_CAP       equ 512            ; text buffer capacity, bytes
NP_IOCAP     equ NP_CAP * 2     ; staging capacity: every char may become CR LF
NP_BSS_TOTAL equ 532 + NP_IOCAP ; see the bss layout after OS88_IMAGE_END
NP_MARGIN    equ 6              ; left/top text margin inside the content
NP_KEY_SAVE  equ 0x3C           ; F2 scan code (DOS Editor's keys)
NP_KEY_LOAD  equ 0x3D           ; F3

; -----------------------------------------------------------------------------
; np_entry - package entry point (SPEC.md 20.2)
; in:  DS=ES=KERNEL_SEG, IF=1, gfx lock NOT held
; out: BX = window ptr, CF clear (CF set = abort, propagated from wm_create)
; The loader wm_shows the window; we must not show, draw or spawn here. The
; bss arrives zeroed, which is already a fresh empty note - the built-in's
; KD_INIT proc (app_note_kinit) had nothing else to do either.
; -----------------------------------------------------------------------------
np_entry:
    push si
    mov si, np_tpl
    call OSAPI_WM_CREATE            ; BX = window ptr, CF on table full
    pop si
    jc .out                         ; table full: nothing to flag
    push ax
    mov al, 1                       ; resizable (SPEC.md 11.1/27): np_paint
    call OSAPI_WM_SIZABLE           ; already lays out from the live record,
    pop ax                          ; so the next repaint re-wraps for free
    clc                             ; CF must still report the create result
.out:
    ret

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
    add ax, NP_MARGIN
    mov [np_tx], ax                 ; text origin x = the wrap column
    mov di, ax                      ; DI = pen x
    add dx, NP_MARGIN
    mov bp, dx                      ; BP = pen y
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    sub ax, 2
    mov [np_rgt], ax                ; content right (inclusive)
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    sub ax, 2
    mov [np_bot], ax                ; content bottom (inclusive)

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
    ret

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
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    sub ax, 4                   ; 2px frame + a 2px gap from the edge
    mov [np_bx2], ax
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
    mov word [np_msg], np_m_saved
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
    mov word [np_msg], np_m_loaded
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
    call np_load
    jmp short .redraw
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
    mov bx, si
    call OSAPI_WM_CONTENT           ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                       ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                       ; DX = y2
    pop bx                          ; BX = y1
    push ax                         ; the pen is a register here, not a
    mov al, CWHITE                  ; variable - keep x1 across the call
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL             ; white-fill the content
    call np_paint                   ; SI still = window ptr
    mov bx, si                      ; the white fill erased the grow box;
    call OSAPI_WM_GROW              ; restore it (SPEC.md 11.1/27)

.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
; Same geometry the built-in used: 260x180 outer -> 258x160 content.
np_tpl:
    dw 60, 60, 260, 180
    dw np_ttl, np_paint, np_onkey, 0

np_ttl: db 'Note Pad', 0

; --- the file and what the toast can say (SPEC.md 27.1) ------------------------
; One fixed name: a package has no text-entry control to ask for another,
; and every instance sharing the one file is visible, predictable behaviour.
np_name:     db 'NOTES.TXT', 0
np_m_saved:  db 'Saved NOTES.TXT', 0
np_m_loaded: db 'Loaded NOTES.TXT', 0
np_m_trunc:  db 'Truncated', 0

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
np_pad      equ os88_image_end + 530   ; word: keeps np_io on an even offset
np_io       equ os88_image_end + 532   ; NP_IOCAP bytes: the CR LF staging
                                       ; buffer, both directions
                                       ; total 532 + NP_IOCAP = NP_BSS_TOTAL
