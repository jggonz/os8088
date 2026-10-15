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
    call at_afit                    ; ...and gives its arena back (46.6.3)
    mov si, at_s_defname            ; UNTITLED.MD - what Save As offers
    mov di, at_name
.name:
    lodsb
    mov [di], al
    inc di
    or al, al
    jnz .name
    call at_layout
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

; -----------------------------------------------------------------------------
; at_onclose - W_ONCLOSE: the close box asks too (SPEC.md 46.7.1, 75.1)
; in:  SI = our window; the UI task, gfx lock HELD, and it must not take it
; out: CF = 1 REFUSED (nothing is closed); CF = 0 go ahead
;
; It asks with os88ui_ask and not with at_alert, and the reason is geometry:
; at_alert centres on [at_vw]/[at_vh], the SCREEN, because it is drawn on the
; fullscreen surface this app owns at the time. A close box is never clicked
; on that surface - a WF_FULL window has no chrome and covers the dock and the
; bar - so this runs WINDOWED or MINIMIZED, where at_alert would paint outside
; our window and route no input to itself. SPEC.md 75.3's alert is a real
; window and works in both, and 75.3.1's refusal then RAISES the question and
; un-minimizes it, which is what a second click on a docked tile's Close
; should do.
; -----------------------------------------------------------------------------
at_onclose:
    cmp byte [at_dirty], 0
    je .go                          ; nothing unsaved: nothing to say
    push ax
    push bx
    push si
    push di
    mov bx, si                      ; BX = our window; SI becomes the message
    mov si, at_s_askclose
    mov di, at_onask
    mov al, OS88UI_ASAVE
    call os88ui_ask                 ; ...and asked EVEN IF ONE IS UP: the
    pop di                          ; refusal raises the one that is
    pop si
    pop bx
    pop ax
    stc                             ; ...and this close does not happen either
    ret                             ; way - a refusal is not a hide (75.1)
.go:
    clc
    ret

; -----------------------------------------------------------------------------
; at_onask - the close alert's completion (SPEC.md 75.3)
; in:  AL = the answer, SI = our window; UI task, gfx lock HELD, alert gone
; out: nothing
; -----------------------------------------------------------------------------
at_onask:
    or al, al
    jz .save                        ; 0 = Save (the default, and Enter)
    cmp al, 1
    je .close                       ; 1 = Discard
    ret                             ; 2 = Cancel, or OS88UI_ACANCEL (dismissed):
                                    ; stay open, and the document is untouched
.save:
    cmp byte [at_havefile], 0
    je .askname                     ; untitled: ASK where it goes. The SPEC.md
                                    ; 38 dialog is a real window, so it works
                                    ; with no surface of ours, and its
                                    ; completion continues the close
    call at_save_q                  ; titled: write it, and say NOTHING on
    jc .failed                      ; failure - there is no surface to say it on
.close:
    call at_do_close
    ret
.askname:
    mov byte [at_pend], 4           ; ...and the commit is a CLOSE
    call at_dlg_save
    ret
.failed:
    push ax                         ; the write's verdict, in a WINDOW for
    push bx                         ; at_onclose's own reason. We stay open and
    push si                         ; the document is exactly as it was, which
    push di                         ; is the whole point of having asked
    mov ax, [at_ferr]
    call at_ferrmsg                 ; AX = FERR_* -> SI = message
    mov bx, [at_win]
    xor di, di                      ; no completion: OK just goes away
    mov al, OS88UI_AOK
    call os88ui_ask
    pop di
    pop si
    pop bx
    pop ax
    ret

; at_do_close - spend the close (SPEC.md 75.2): DEFERRED, so this returns and
; the window goes on the next UI pass
at_do_close:
    push bx
    mov byte [at_pend], 0
    mov bx, [at_win]
    call OSAPI_WM_CLOSE
    pop bx
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
    cmp al, 4
    je .close                       ; SPEC.md 46.7.1: the close box's Save,
    jmp short .out                  ; continued through the Save As dialog
.new:
    call at_cmd_newdoc
    call at_fs_paint_all            ; fresh page (fullscreen only)
    jmp short .out
.open:
    call at_dlg_open
    jmp short .out
.quit:
    call at_fs_exit
    jmp short .out
.close:
    call at_do_close
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
; at_save_q - write the document to [at_name], SAYING NOTHING
; out: CF = 0 written ([at_dirty] cleared); CF = 1 refused, with [at_ferr] =
;      FERR_*. Every register preserved - the code goes in [at_ferr] for the
;      reason at_dlg_done banks one there, that it has to survive this frame's
;      pops (SPEC.md 46.7.1)
; -----------------------------------------------------------------------------
at_save_q:
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
    push ax                         ; PIN it across the write (66.5.7.1): the
    xor ax, ax                      ; source below is a pointer INTO this
    call at_dmov                    ; claim and OSAPI_FILE_WRITE holds it
    pop ax                          ; across the sector cache's own claims
                                    ; (SPEC.md 18.95), which can compact
    mov es, [at_dseg]               ; ES:BX = the document itself - no staging
    xor bx, bx                      ; pass, because the gap is parked at the
    xor dx, dx                      ; end and [0..len) is one straight run
    mov si, at_name                 ; (SPEC.md 18.4.1/46.9)
    call OSAPI_FILE_WRITE
    push ax                         ; movable again on BOTH paths out, before
    mov ax, at_reloc                ; the CF below picks one
    call at_dmov
    pop ax
    jnc .ok
    mov [at_ferr], ax               ; ...and NOTHING IS SAID here: at_save_named
    stc                             ; below is what says it, and the close
    jmp short .out                  ; negotiator says it another way entirely
.ok:
    mov byte [at_dirty], 0
    clc
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
; at_save_named - at_save_q, and complain ON THE SURFACE if it failed
; out: CF = 1 it did not go; the alert is up and [at_modal] says so. Every
;      register preserved, as before the split
;
; Split from at_save_q for SPEC.md 46.7.1: the close negotiator runs when there
; is no surface to draw an alert on - a close box is never clicked on a
; fullscreen window, because a WF_FULL window has no chrome and covers the dock
; and the bar - so it needs the write's verdict WITHOUT at_alert's
; screen-centred box, and says it in a window of its own instead.
; -----------------------------------------------------------------------------
at_save_named:
    push ax
    push si
    call at_save_q
    jnc .out
    mov ax, [at_ferr]               ; AX = FERR_* -> SI = message
    call at_ferrmsg
    mov al, AT_MD_ERR
    call at_alert
    stc
.out:
    pop si
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
    push ax                         ; PIN it across the read, at_save_named's
    xor ax, ax                      ; reasoning exactly (SPEC.md 66.5.7.1) -
    call at_dmov                    ; and AFTER at_dresize above, whose regrow
    pop ax                          ; is unaffected either way
    mov es, [at_dseg]
    xor bx, bx
    mov cx, [at_dcap]
    xor dx, dx                      ; DX:CX = the capacity; a file bigger than
    mov si, at_name                 ; the gap buffer is FERR_BIG, as before
    call OSAPI_FILE_READ            ; DX:AX = bytes read, and DX is 0 here
    push ax
    mov ax, at_reloc
    call at_dmov
    pop ax
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
    call at_afit                    ; a new history: its arena back (46.6.3)
    call at_dfit                    ; give back what the file did not need
    call at_layout
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
; SPEC.md 46.7.1, and OS88UI_AMAX is 34 characters: the alert CLIPS rather
; than refuses, so this is counted rather than trusted - 28 with the NUL
at_s_askclose: db 'Save changes before closing?', 0
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
; at_arg - accept a document handed to us at launch (SPEC.md 54.5)
; in:  nothing; called from at_entry once the window and the claim exist
; out: nothing; preserves all registers AND the flags - the loader's CF is
;      still riding in them
;
; It RECORDS and does not load. at_load_named repaints and can enter
; fullscreen, which want the gfx lock HELD, and an entry proc holds none
; (SPEC.md 20.2) - at_onwake spends it, where the window is placed, the
; geometry is known AND the splash is already on the glass. The first W_PAINT
; spent it until SPEC.md 54.10, and that paint runs inside wm_show's own
; repaint, so the read still happened before the desktop was finished.
; -----------------------------------------------------------------------------
at_arg:
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    call OSAPI_ARG_FILE             ; CF=1 = launched empty, the usual case
    jc .out
    mov [at_argclus], dx
    mov [at_argdrv], bl
    mov ax, KERNEL_SEG              ; the name is a KERNEL pointer, so ES is
    mov es, ax                      ; loaded rather than trusted
    mov di, at_name
    mov cx, 13
.copy:
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .named
    inc si
    inc di
    loop .copy
    mov byte [di], 0
.named:
    push ds
    pop es                          ; ES = DS again, the callback default
    mov byte [at_argp], 1
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; -----------------------------------------------------------------------------
; at_onwake - W_ONWAKE: open the launch document (SPEC.md 54.10/74.1)
; in:  SI = our window; the UI task, gfx lock NOT held
; out: nothing; no register need be preserved
;
; The kernel calls this once assoc_run has shown our window, so the read
; happens in front of an Artful the user can see - the splash, drawn by the
; W_PAINT wm_show already ran - and SPEC.md 12.8's widget steps through it. It
; takes the lock for the burst SPEC.md 74.1 lets it state: at_load_named reads
; and at_fs_enter takes the screen, and both draw.
; -----------------------------------------------------------------------------
at_onwake:
    cmp byte [at_fsowed], 0         ; SPEC.md 46.5.3: the dock gave us back, and
    je .arg                         ; the document is where we are going
    mov byte [at_fsowed], 0         ; cleared FIRST: at_fs_enter can be refused
    call OSAPI_GFX_LOCK             ; (another window owns the screen) and a
    call at_fs_enter                ; flag left set would post a wake from the
    cmp byte [at_fs], 0             ; next paint for ever. A refusal leaves
    jne .fsdone                     ; [at_fs] at 0 - and here, unlike
    mov bx, [at_win]                ; at_argload one caller along, NO splash is
    call OSAPI_WM_CLIP_SET          ; on the glass: at_paint's .win arm drew
    jc .fsdone                      ; NOTHING and the kernel white-filled the
    call at_splash                  ; content, so a refusal has to draw it
.fsdone:                            ; itself or the window stays a blank card
    call OSAPI_GFX_UNLOCK           ; (at_abdismiss's own idiom, SPEC.md 11.3)
    ret
.arg:
    cmp byte [at_argp], 0           ; not the document's wake, or the document
    je .out                         ; is already open
    call OSAPI_GFX_LOCK
    call at_argload
    call OSAPI_GFX_UNLOCK
.out:
    ret

; -----------------------------------------------------------------------------
; at_argload - spend it, from at_onwake (SPEC.md 54.10)
; in:  the gfx lock HELD, the window visible AND DRAWN
; out: CF=1 = the screen is already painted; CF=0 = nothing happened and the
;      splash W_PAINT already drew is still the honest picture. All registers
;      preserved.
;
; The load AND at_ondlg's fullscreen entry, because the entry is what makes
; the document visible at all: [at_fs] is 0 at launch, so at_paint's own test
; sends it to at_splash, and without the entry a perfectly loaded document
; would sit behind one. at_fs_enter repaints everything itself (through
; W_PAINT, re-entering with [at_argp] already spent), which is what the CF
; used to be for when the caller WAS a paint proc; it is kept because
; at_ondlg's caller reads it too.
;
; A failure of any kind - unreachable folder, unreadable file, or a
; fullscreen refused because another window owns the screen - leaves
; [at_fs] at 0, and that one test answers all three: the splash already on the
; glass is then the honest thing to have shown, over the fresh untitled
; document at_cmd_newdoc already made, with no alert over a window that has
; not been seen yet.
; -----------------------------------------------------------------------------
at_argload:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov byte [at_argp], 0           ; once, whatever happens below
    mov dx, [at_argclus]
    mov bl, [at_argdrv]
    call OSAPI_FILE_GOTO_QM         ; the folder it was opened from -
    jc .out                         ; QUIETLY, and the instance with us, so
                                    ; at_load_named's by-name read resolves
                                    ; there. Nothing here lists the folder,
                                    ; so the remount's scan, sort and icon
                                    ; harvest were ~0.5 s of floppy for no
                                    ; reader (docs/plans/NAV-COST-PLAN.md,
                                    ; SPEC.md 19.9.1). CF=1 moved NOTHING:
                                    ; the read would find [at_name] in our
                                    ; own folder, so refuse. Every register
                                    ; is banked above (QM's AX, and a
                                    ; redirected volume's CX, SI and DI -
                                    ; SPEC.md 62.9.1)
    call at_load_named
    jc .out
    call at_fs_enter                ; ...which paints everything itself
.out:
    pop es                          ; whatever at_load_named left in ES, the
                                    ; caller's is what goes back
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [at_fs], 0             ; did we reach the editor? (POP leaves the
    je .nofs                        ; flags alone, so this is the only test)
    stc
    ret
.nofs:
    clc
    ret

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
