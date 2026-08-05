; =============================================================================
; os8088 - apps/artful/atfile.inc
;
; ArtfulType's file layer (SPEC.md 46.7): New / Open / Save / Save As over
; the file slots and the Standard File dialog, plus the "Save changes?"
; pending-action flow (file.c's ConfirmDiscardChanges). Documents are plain
; markdown text; the internal newline is LF (10), written as-is - the
; original's pitch is moving .md files to a MODERN machine, where bare LF
; is the native ending - and CR LF or lone CR fold to LF on the way in, so
; files written by DOS-side tools load right too. Control bytes and
; anything past 126 are dropped at load (the renderer's glyph range).
;
; All of it runs on the UI task from callbacks - the file slots' one
; binding context rule (SPEC.md 18.4).
; =============================================================================

; -----------------------------------------------------------------------------
; at_cmd_newdoc - reset to an untitled empty document (no confirmation)
; -----------------------------------------------------------------------------
at_cmd_newdoc:
    push ax
    push cx
    push si
    push di
    mov word [at_gs], 0             ; empty FIRST - the whole buffer is gap -
    mov ax, [at_dcap]               ; so the shrink below has no high run to
    mov [at_ge], ax                 ; carry down and cannot refuse
    mov ax, AT_KB0                  ; ...and this is the moment to give the
    call at_dresize                 ; heap back whatever the old document
    mov ax, [at_dcap]               ; grew into (SPEC.md 46.9)
    mov [at_ge], ax
    mov word [at_caret], 0
    mov word [at_sela], 0
    mov word [at_selb], 0
    mov word [at_top], 0
    mov byte [at_dirty], 0
    mov byte [at_havefile], 0
    mov byte [at_typrun], 0
    mov word [at_ucnt], 0           ; a new document starts a new history
    mov word [at_utop], 0
    mov word [at_rcnt], 0
    mov word [at_rtop], 0
    mov si, at_s_defname            ; UNTITLED.MD - what Save As offers
    mov di, at_name
.name:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .name
    call at_layout
    call at_cline_span
    pop di
    pop si
    pop cx
    pop ax
    ret

; the New / Open / Quit commands ask about unsaved changes first
at_cmd_new:
    push ax
    mov al, 1
    call at_confirm
    pop ax
    ret
at_cmd_open:
    push ax
    mov al, 2
    call at_confirm
    pop ax
    ret
at_cmd_quit:
    push ax
    mov al, 3
    call at_confirm
    pop ax
    ret

; at_confirm - run pending action AL now, or after the Save-changes alert
at_confirm:
    push si
    mov [at_pend], al
    cmp byte [at_dirty], 0
    jne .ask
    call at_runpend
    jmp short .out
.ask:
    push ax
    mov al, AT_MD_SAVE
    xor si, si
    call at_alert
    pop ax
.out:
    pop si
    ret

; at_runpend - the pending action, once the changes question is settled
at_runpend:
    push ax
    mov al, [at_pend]
    mov byte [at_pend], 0
    cmp al, 1
    je .new
    cmp al, 2
    je .open
    cmp al, 3
    je .quit
    jmp short .out
.new:
    call at_cmd_newdoc
    call at_fs_paint_all            ; fresh page (fullscreen only)
    jmp short .out
.open:
    call at_dlg_open
    jmp short .out
.quit:
    call at_fs_exit
.out:
    pop ax
    ret

; at_savepend - the alert's Save button: save, then continue.
; A titled document saves synchronously and runs the pending action here;
; an untitled one raises Save As, and the completion proc continues it.
at_savepend:
    cmp byte [at_havefile], 0
    je .askname
    call at_save_named
    cmp byte [at_modal], 0          ; a failed save alerts and stops: the
    jne .out                        ; pending action must not eat the doc
    call at_runpend
.out:
    ret
.askname:
    call at_dlg_save
    ret

; -----------------------------------------------------------------------------
; at_cmd_save / at_cmd_saveas - the File menu's two save doors
; -----------------------------------------------------------------------------
at_cmd_save:
    mov byte [at_pend], 0           ; a direct save continues nothing: a
                                    ; pend left by a CANCELLED dialog (which
                                    ; calls nobody, SPEC.md 38) or a failed
                                    ; titled save must not fire here
    cmp byte [at_havefile], 0
    je at_cmd_saveas
    call at_save_named
    ret
at_cmd_saveas:
    mov byte [at_pend], 0
    call at_dlg_save
    ret

; -----------------------------------------------------------------------------
; at_save_named - write the document to [at_name]
; out: [at_dirty] cleared on success; a failure raises the error alert
; -----------------------------------------------------------------------------
at_save_named:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    ; the document is contiguous only around the gap: park the gap at the
    ; end, and [0..len) is one straight run at at_doc
    call at_dlen
    call at_gapto                   ; gap to the end - text is contiguous
    mov cx, ax                      ; wait: at_gapto wants AX = position =
    ; the length itself; AX already holds it. CX = byte count for write.
    mov es, [at_dseg]               ; ES:BX = the document itself - no staging
    xor bx, bx                      ; pass, because the gap is parked at the
    xor dx, dx                      ; end and [0..len) is one straight run
    mov si, at_name                 ; (SPEC.md 18.4.1/46.9)
    call OSAPI_FILE_WRITE
    jc .err
    mov byte [at_dirty], 0
    jmp short .out
.err:
    call at_ferrmsg                 ; AX = FERR_* -> SI = message
    push ax
    mov al, AT_MD_ERR
    call at_alert
    pop ax
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
; at_load_named - read [at_name] into the document
; out: CF=0 loaded (layout refreshed, undo cleared); CF=1 with AX = FERR_*
;      (the caller alerts - it knows what is on screen at the time)
; The file lands in the buffer's front half straight off the disk, then
; folds in place: CR LF -> LF, lone CR -> LF, tab becomes one space, and
; everything else outside 32..126 drops (the renderer's glyph range).
; -----------------------------------------------------------------------------
at_load_named:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov ax, AT_MAXKB                ; open the claim to its ceiling for the
    call at_dresize                 ; read: nothing knows the file's size
                                    ; until the read reports it, and the fold
                                    ; below only ever SHRINKS what arrived, so
                                    ; the file lands in the document buffer
                                    ; itself (SPEC.md 46.9). at_dfit gives the
                                    ; rest back at the end of both paths
    mov es, [at_dseg]
    xor bx, bx
    mov cx, [at_dcap]
    xor dx, dx                      ; DX:CX = the capacity; a file bigger than
    mov si, at_name                 ; the gap buffer is FERR_BIG, as before
    call OSAPI_FILE_READ            ; DX:AX = bytes read, and DX is 0 here
    jc .err
    ; fold in place: SI reads, DI writes, both inside the document claim
    mov cx, ax
    xor si, si
    xor di, di
    xor dl, dl                      ; DL = previous raw byte
.fold:
    jcxz .folded
    es lodsb                        ; the document is ES, not DS (SPEC.md 46.9)
    dec cx
    cmp al, 10
    jne .notlf
    cmp dl, 13
    je .skip                        ; CR LF: the CR already wrote the LF
    jmp short .store
.notlf:
    cmp al, 13
    jne .noteol
    mov dl, al                      ; remember the CR...
    mov al, 10                      ; ...and store it as LF
    mov [es:di], al
    inc di
    jmp short .fold
.noteol:
    cmp al, 9
    jne .nottab
    mov al, ' '
    jmp short .store
.nottab:
    cmp al, 32
    jb .skip
    cmp al, 126
    ja .skip
.store:
    mov [es:di], al
    inc di
.skip:
    mov dl, al
    jmp short .fold
.folded:
    mov [at_gs], di                 ; text = [0..kept), gap = the rest
    mov ax, [at_dcap]
    mov [at_ge], ax
    mov word [at_caret], 0
    mov word [at_sela], 0
    mov word [at_selb], 0
    mov word [at_top], 0
    mov byte [at_dirty], 0
    mov byte [at_havefile], 1
    mov byte [at_typrun], 0
    mov word [at_ucnt], 0
    mov word [at_utop], 0
    mov word [at_rcnt], 0
    mov word [at_rtop], 0
    call at_dfit                    ; give back what the file did not need
    call at_layout
    call at_cline_span
    clc
    jmp short .out
.err:
    mov [at_ferr], ax               ; the code survives this frame's pops
    cmp ax, FERR_IO                 ; a mid-chain FERR_IO already clobbered
    jne .keep                       ; part of the document (only FERR_BIG and
    call at_cmd_newdoc              ; the pre-I/O refusals leave it alone):
                                    ; the old text is gone, so become a fresh
                                    ; Untitled rather than a hybrid a later
                                    ; Save would silently write over the file
.keep:
    call at_dfit                    ; ...and do not leave AT_MAXKB held for a
    stc                             ; load that did not happen
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; at_ferrmsg - FERR_* in AX -> SI = the human string (notepad's table idiom)
at_ferrmsg:
    push bx
    cmp ax, FERR_BIG
    jbe .known
    mov ax, FERR_IO
.known:
    mov bx, ax
    shl bx, 1
    mov si, [bx+at_ferrtab]
    pop bx
    ret

at_ferrtab:
    dw at_e_ok, at_e_nodisk, at_e_io, at_e_name, at_e_noent, at_e_exist
    dw at_e_full, at_e_dirfull, at_e_prot, at_e_wprot, at_e_big
at_e_ok:      db 'Done', 0
at_e_nodisk:  db 'No data disk in the drive.', 0
at_e_io:      db 'A disk error occurred.', 0
at_e_name:    db 'That name is not valid.', 0
at_e_noent:   db 'File not found.', 0
at_e_exist:   db 'That name already exists.', 0
at_e_full:    db 'The disk is full.', 0
at_e_dirfull: db 'The folder is full.', 0
at_e_prot:    db 'That file is protected.', 0
at_e_wprot:   db 'The disk is write-protected.', 0
at_e_big:     db 'Too large to open here.', 0

; -----------------------------------------------------------------------------
; at_dlg_open / at_dlg_save - raise the Standard File dialog (SPEC.md 38.6)
; A refusal (one is already up) is silently nothing, notepad's rule.
; -----------------------------------------------------------------------------
at_dlg_open:
    push ax
    push bx
    push si
    push di
    mov al, FDLG_OPEN
    jmp short at_dlg_go
at_dlg_save:
    push ax
    push bx
    push si
    push di
    mov al, FDLG_SAVE
at_dlg_go:
    mov bx, [at_win]
    mov di, at_ondlg
    mov si, at_name
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; at_ondlg - the dialog's completion proc (SPEC.md 38.6)
; in:  AL = mode, SI = our window ptr, ES:DI = the chosen name (kernel's
;      buffer - copy it out through ES); UI task, gfx lock HELD, the
;      dialog already gone. Returns with a near ret (the header dispatcher retfs).
; -----------------------------------------------------------------------------
at_ondlg:
    mov bl, al                      ; BL = the mode
    ; copy the name, bounded (notepad's rule: trust nothing)
    mov dx, si                      ; (SI is the window; ours already)
    mov si, di
    mov di, at_name
    mov cx, 13
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .copied
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.copied:
    push ds                         ; the name is copied out; ES was
    pop es                          ; KERNEL_SEG (SPEC.md 38.6) - ours now
    or bl, bl
    jnz .save
    ; open: load, then show the result - entering fullscreen when the
    ; splash raised the dialog. A failure alerts over a freshly painted
    ; editor, never over the hole the dialog left.
    call at_load_named
    jc .loaderr
    cmp byte [at_fs], 0
    je .enter
    call at_fs_paint_all
    jmp short .done
.enter:
    call at_fs_enter                ; paints everything itself
    jmp short .done
.loaderr:
    cmp byte [at_fs], 0
    jne .lefs
    call at_fs_enter                ; the empty editor hosts the alert
    jmp short .lealert
.lefs:
    call at_fs_paint_all
.lealert:
    mov ax, [at_ferr]
    call at_ferrmsg
    mov al, AT_MD_ERR
    call at_alert
    jmp short .done
.save:
    ; a save completion only ever runs fullscreen (the dialog is modal, so
    ; the editor cannot leave while it is up): repaint first, save, then
    ; any failure alert sits on a clean page
    mov byte [at_havefile], 1
    call at_fs_paint_all
    call at_save_named
    cmp byte [at_modal], 0
    jne .done                       ; failed: the alert is up, pend waits
    call at_runpend                 ; Save As inside a quit/new/open flow
.done:
    ret
