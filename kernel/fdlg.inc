; =============================================================================
; os8088 - fdlg.inc
;
; The Standard File dialog (SPEC.md 38): one modal window that lists a
; directory and hands a chosen 8.3 name back to whichever application asked
; for it, in an Open form and a Save form. Label prefix fdlg_.
;
; It is the missing half of SPEC.md 18.4. The file slots gave packages five
; whole-file operations and no way to NAME a file, which is why Note Pad
; wrote a hard-coded NOTES.TXT (SPEC.md 27.1) - not because one note is
; enough, but because there was nothing to ask a filename with.
;
; It is NOT an instance (SPEC.md 38.1). No KIND_*, no inst_tab record, no
; kind-descriptor row: it is a bare wm_create'd window this module owns, and
; wm_owner never names it. A modal dialog is not something that RUNS - it is
; a question the kernel asks on the front application's behalf and then
; forgets, the same species as a menu_track pull-down. Three things fall
; out, all of them wanted:
;
;   - inst_win_owner answers 0 for an unowned window, so the SPEC.md 11
;     dispatch sites skip inst_charge and the time spent deciding is
;     unbilled - like menu_track, and for the same reason.
;   - app_close_win on an unowned window degrades to a plain wm_hide, and
;     fdlg_gate below turns a hidden dialog into a CANCELLED one. So the
;     close box works, and this module contains no close-box code at all.
;   - so does the minimize box, by the same route. A modal dialog that could
;     be minimized would strand the machine behind an invisible modal;
;     instead it simply cancels.
;
; Modality is the load-bearing part (SPEC.md 38.2). [fdlg_win] non-zero
; means the system is input-modal to that window, enforced at exactly two
; points in the UI ladder - fdlg_grab on every button press, fdlg_top on the
; keyboard poll - and nowhere else in the kernel. Both start at fdlg_gate,
; which drops the gate the moment it names a window that is not
; used-and-visible: the menu_check discipline of SPEC.md 12.3, with teeth.
;
; And it is what makes the listing free. Because nothing else is clickable,
; no other window can navigate the volume while a dialog is up, so this
; module needs NO cache of its own: it reads disk_dir / disk_nfiles - the
; SPEC.md 19 mount snapshot - directly, one staged entry at a time. That is
; the exact opposite of the Disk window's rule (SPEC.md 22.1) and it holds
; for exactly this reason: remove modality and the dialog needs a fifth
; VIEW_SEG slot, which does not exist.
;
; fdlg_open, the window procs and fdlg_commit all run with the gfx lock
; already held by the caller and never take it; fdlg_grab is the one entry
; point called with no lock, and it takes its own when it has cleaning up
; to do.
; =============================================================================

; --- geometry (SPEC.md 38.3) -------------------------------------------------
; The window does not resize (WF_SIZABLE is deliberately not set), so the
; whole of the Disk window's live-layout apparatus collapses into these.
; Content is 298 x 151: W-2 wide, H-TITLE_H-1 tall.
FD_X        equ 90              ; window origin
FD_Y        equ 60
FD_W        equ 300
FD_H        equ 170

FD_LX1      equ 6               ; the list frame, content-relative
FD_LY1      equ 20
FD_LX2      equ 213
FD_LY2      equ 120
FD_ROW0     equ 22              ; first row's top; 6 rows of 16 end at 117
FD_ROWH     equ 16
FD_ROWS     equ 6
FD_SBX      equ 200             ; scroll bar: FD_SBX .. FD_LX2, 14 wide
FD_TRKY1    equ FD_LY1+12       ; track between the two arrow-cell rules
FD_TRKY2    equ FD_LY2-12
FD_TRKH     equ FD_TRKY2-FD_TRKY1+1

FD_BX1      equ 224             ; the button column, 62 x 14 each
FD_BX2      equ 286
FD_BY0      equ 20              ; Open / Save (the default button)
FD_BY1      equ 40              ; Cancel
FD_BY2      equ 60              ; Drive
FD_BH       equ 13

FD_NBX1     equ 72              ; the name box
FD_NBX2     equ 213
FD_NBY1     equ 124
FD_NBY2     equ 140
FD_NTX      equ 76              ; text inside it
FD_NTY      equ 128

FD_NAMEMAX  equ 12              ; 8 + '.' + 3, the whole 8.3 budget
FD_DBLCLK   equ 9               ; double-click window, ticks (as SPEC.md 22)

; -----------------------------------------------------------------------------
; fdlg_tpl - the window template (SPEC.md 11): x, y, w, h, title, paint,
; onkey, onclick. The title word is overwritten per mode by fdlg_open.
; -----------------------------------------------------------------------------
fdlg_tpl:   dw FD_X, FD_Y, FD_W, FD_H
            dw fdlg_s_topen, fdlg_paint, fdlg_onkey, fdlg_onclick

fdlg_s_topen:  db 'Open', 0
fdlg_s_tsave:  db 'Save As', 0
fdlg_s_open:   db 'Open', 0
fdlg_s_save:   db 'Save', 0
fdlg_s_cancel: db 'Cancel', 0
fdlg_s_drive:  db 'Drive', 0
fdlg_s_lopen:  db 'Open:', 0
fdlg_s_lsave:  db 'Save as:', 0
fdlg_s_dotdot: db '..', 0
fdlg_s_folder: db 'folder', 0
fdlg_s_up:     db 'up', 0
fdlg_s_root:   db 'Disk', 0
fdlg_s_sub:    db 'Folder', 0
fdlg_s_nodisk: db 'no disk', 0
fdlg_s_drva:   db 'A:', 0
fdlg_s_drvb:   db 'B:', 0

; -----------------------------------------------------------------------------
; fdlg_win - the live dialog window, 0 = none, AND the modal gate.
;
; It lives HERE, in .text with an initialiser, and not in .bss with the rest
; of the module's state - the [ui_pcmd] precedent of SPEC.md 13. `-f bin`
; gives .bss no image bytes and nothing zeroes it at boot, and this word is
; read by fdlg_grab on the very first mouse press the machine ever sees. As
; garbage it is a window pointer, and the ladder would go modal to a window
; that does not exist before the desktop has been clicked once.
;
; Everything else in this module is read only when this word is non-zero,
; which only fdlg_open makes true - and fdlg_open writes all of it first.
; This is the one word with no such guard, because it IS the guard.
; -----------------------------------------------------------------------------
fdlg_win:   dw 0

; -----------------------------------------------------------------------------
; fdlg_open - API slot 0x0150 (SPEC.md 38.6)
; in:  AL = 0 Open / 1 Save, BX = requester window ptr, DI = completion proc
;      OFFSET in the caller's segment (stored as a value; the SEGMENT is
;      resolved from the requester's instance when the callback is invoked,
;      never stored - SPEC.md 38.6/11), SI = default name (NUL, <= 12) or 0.
;      THE CALLER HOLDS THE GFX LOCK - every legal caller is a window
;      callback or an AM_ONCMD handler, which is what lets this create and
;      show the window inline instead of posting a launch.
; out: CF = 0 accepted and the dialog is already on screen; CF = 1 refused
;      (one is already up, BX is not a live owned window, or the window
;      table is full). All registers preserved.
;
; The staleness triple (record, I_WIN, I_SPTR) is captured here and checked
; again at commit time: a package whose window closes while the dialog is up
; has had its region freed, and its callback offset stops meaning anything.
; The window pointer alone would not do - window slots are reused. I_SPTR is
; a segment since v3 (SPEC.md 29.1); the triple only ever compares it for
; equality, so the unit change costs it nothing.
; -----------------------------------------------------------------------------
fdlg_open:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    cmp word [fdlg_win], 0
    jne .refuse                 ; the gate is a single word on purpose: no
                                ; dialog ever opens on top of another
    mov [fdlg_rqcb], di
    mov [fdlg_rqwin], bx
    mov [fdlg_mode], al

    call inst_win_owner         ; DI = the owning record, or 0; BX preserved
    or di, di
    jz .refuse
    cmp byte [di+I_STATE], 1
    jne .refuse
    mov [fdlg_rqrec], di
    mov ax, [di+I_SPTR]
    mov [fdlg_rqsp], ax

    mov byte [fdlg_nlen], 0     ; seed the name box from the caller's default
    mov byte [fdlg_name], 0
    or si, si
    jz .fresh
    call fdlg_setname
.fresh:
    mov word [fdlg_sel], 0xFFFF
    mov word [fdlg_scrl], 0
    mov word [fdlg_clkt], 0

    mov dl, [disk_drive]        ; re-list where the volume already is: the
    mov ax, [dsk_cwd]           ; snapshot may be another window's folder
    call dsk_chdir              ; (SPEC.md 22.1), and from here until the
                                ; dialog closes nothing else can move it

    mov si, fdlg_tpl
    call wm_create              ; BX = window ptr, CF = table full
    jc .refuse
    mov ax, fdlg_s_topen
    cmp byte [fdlg_mode], 0
    je .titled
    mov ax, fdlg_s_tsave
.titled:
    mov [bx+W_TITLE], ax
    mov [fdlg_win], bx          ; take the gate LAST: until this store the
                                ; window is an ordinary one
    call wm_show                ; raise + repaint, under the caller's lock
    clc
    jmp short .out
.refuse:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_gate - validate the modal gate (internal, SPEC.md 38.2)
; in:  AL = 1 if the caller HOLDS the gfx lock, 0 if not
; out: CF = 0 and BX = the live dialog window; CF = 1 = no dialog is up
;      (BX = 0). Preserves everything else.
;
; A gate window that is no longer used-and-visible is a CANCELLED dialog:
; hiding is the only thing the close box and the minimize box do to an
; unowned window (SPEC.md 29.4), so collecting them here is what lets this
; module carry no close-path code and still never wedge the machine behind
; an invisible modal. Input is the only thing that can observe the gate and
; both kinds of input come through here first, so the collection is never
; late by anything a user can perceive.
; -----------------------------------------------------------------------------
fdlg_gate:
    push cx
    mov bx, [fdlg_win]
    or bx, bx
    jz .none
    mov cx, [bx+W_FLAGS]
    and cx, 3                   ; used AND visible
    cmp cx, 3
    jne .dead
    pop cx
    clc
    ret
.dead:
    or al, al
    jnz .close
    call gfx_lock
    call fdlg_close
    call gfx_unlock
    jmp short .none
.close:
    call fdlg_close
.none:
    xor bx, bx
    pop cx
    stc
    ret

; -----------------------------------------------------------------------------
; fdlg_reap - collect a cancelled dialog (SPEC.md 38.2)
; in:  nothing; NO gfx lock held (the UI task's deferred section)
; out: nothing (all registers preserved)
;
; The close box and the minimize box only HIDE this window (SPEC.md 38.1),
; and fdlg_gate is what turns that into a cancel - but the gate is only
; consulted by input, and "until the user clicks something" is too long to
; leave a dialog un-collected: the menu bar spends that whole time saying
; Locator, because [menu_win] still names a window nobody can see. Running
; it once per UI pass costs three instructions when no dialog is up and
; makes both effects immediate.
; -----------------------------------------------------------------------------
fdlg_reap:
    cmp word [fdlg_win], 0
    je .out
    push ax
    push bx
    xor al, al                  ; no lock held: fdlg_gate takes its own
    call fdlg_gate
    pop bx
    pop ax
.out:
    ret

; -----------------------------------------------------------------------------
; fdlg_grab - the modal click filter (SPEC.md 38.2)
; in:  CX = press x, DX = press y; NO gfx lock held (the UI ladder's own
;      state at the top of the EVT_MDOWN / EVT_RDOWN branches)
; out: CF = 1 = swallow this press. All registers preserved.
;
; Deliberately a rect test and not a wm_hit: modality is not a window
; property, and a press on the menu bar has to be swallowed exactly as
; firmly as a press on another window. A press INSIDE the dialog passes
; through to the ordinary ladder, which is where its title-bar drag and its
; content clicks come from for free.
; -----------------------------------------------------------------------------
fdlg_grab:
    push ax
    push bx
    xor al, al                  ; no lock held
    call fdlg_gate
    jc .pass
    cmp cx, [bx+W_X]
    jl .swallow
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    cmp cx, ax
    jge .swallow
    cmp dx, [bx+W_Y]
    jl .swallow
    mov ax, [bx+W_Y]
    add ax, [bx+W_H]
    cmp dx, ax
    jge .swallow
.pass:
    clc
    jmp short .out
.swallow:
    call snd_beep
    stc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_top - the modal keyboard filter (SPEC.md 38.2)
; in:  BX = wm_top's answer; the caller HOLDS the gfx lock
; out: BX = the dialog window if one is up, else BX unchanged. Preserves
;      everything else.
; Belt to fdlg_grab's braces - nothing can be raised above a dialog while
; every click outside it is being swallowed - but ten bytes is a cheap price
; for a keystroke never reaching the window behind.
; -----------------------------------------------------------------------------
fdlg_top:
    push ax
    push cx
    mov cx, bx                  ; keep wm_top's answer: fdlg_gate zeroes BX
    mov al, 1                   ; the lock is held
    call fdlg_gate
    jnc .out
    mov bx, cx
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_close - tear the dialog down (SPEC.md 38.7)
; in:  nothing; caller HOLDS the gfx lock
; out: nothing (all registers preserved)
;
; The menu bar goes back to the requester because wm_front handed it to US
; on the way in (SPEC.md 12) and this window has no menus of its own - left
; alone the bar would say Locator until the user happened to click the
; application again. If the requester died meanwhile, menu_draw_bar's own
; menu_check reverts to Locator, which is the right answer then.
; -----------------------------------------------------------------------------
fdlg_close:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, [fdlg_win]
    or bx, bx
    jz .out
    mov word [fdlg_win], 0      ; drop the gate FIRST: wm_destroy repaints,
    call wm_destroy             ; and nothing may observe a gate pointing at
                                ; a freed record
    mov bx, [fdlg_rqwin]
    call menu_activate
    call menu_draw_bar
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_commit - close, then answer the application (SPEC.md 38.6/38.7)
; in:  fdlg_name holds the answer; caller HOLDS the gfx lock and is INSIDE
;      the dialog's own window callback
; out: nothing (all registers preserved)
;
; Teardown first, callback second, and the order is binding: wm_destroy's
; repaint has already restored everything the dialog covered, so the
; callback paints onto a clean window instead of over a dialog that is still
; on screen. Destroying the window from inside its own W_ONCLICK is safe
; because the SPEC.md 11/13 dispatch sites read the proc pointer BEFORE the
; call and touch nothing but the saved owner word afterwards - and that word
; is 0 for this window (SPEC.md 38.1), so there is not even an inst_charge
; to land in a freed record.
; -----------------------------------------------------------------------------
fdlg_commit:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call fdlg_close

    mov di, [fdlg_rqrec]        ; the staleness triple (SPEC.md 38.6)
    or di, di
    jz .out
    cmp byte [di+I_STATE], 1
    jne .out
    mov ax, [fdlg_rqwin]
    cmp ax, [di+I_WIN]
    jne .out
    mov ax, [fdlg_rqsp]
    cmp ax, [di+I_SPTR]
    jne .out
    mov cx, [fdlg_rqcb]
    or cx, cx
    jz .out
    call inst_cb_seg            ; AX = the requester's segment (DI = the
                                ; record the triple just validated) -
                                ; resolved at INVOKE time, never stored
                                ; (SPEC.md 38.6/11)
    push word [snd_inst]        ; stamp the dispatched instance, like every
    call snd_disp_set           ; other callback site (SPEC.md 34.3): sound
                                ; grants made inside the completion proc,
                                ; and inst_pkg_spawn's identity fence
                                ; (SPEC.md 20.6), both read it. DI = the
                                ; requester's record
    push es                     ; the callback owes us nothing (SPEC.md
                                ; 38.6), and ES is the one register the
                                ; kernel cannot shrug off (SPEC.md 1)
    push ds
    pop es                      ; ES = KERNEL_SEG: the name travels as
                                ; ES:DI since v3 - a package's DS is its
                                ; own segment and cannot read fdlg_name
                                ; flat any more (SPEC.md 38.6)
    push ax                     ; the resolved segment above the offset:
    push cx                     ; inst_cb_call_es's argument pair
    mov al, [fdlg_mode]         ; the callback's contract: AL = mode,
    mov ah, 0                   ; SI = its window, ES:DI = the name
    mov si, [fdlg_rqwin]
    mov di, fdlg_name
    call inst_cb_call_es        ; near for a kernel requester, far with
                                ; DS = the package segment else - ES stays
                                ; KERNEL_SEG on both legs (SPEC.md 38.6)
    pop es
    pop word [snd_inst]         ; the outer dispatch's stamp, back
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_setname - store a name in the box (internal)
; in:  SI -> NUL-terminated source
; out: fdlg_name / [fdlg_nlen], truncated to FD_NAMEMAX. Preserves all.
; -----------------------------------------------------------------------------
fdlg_setname:
    push ax
    push bx
    push si
    xor bx, bx
.copy:
    mov al, [si]
    or al, al
    jz .done
    cmp bx, FD_NAMEMAX
    jae .done
    mov [bx+fdlg_name], al
    inc bx
    inc si
    jmp short .copy
.done:
    mov byte [bx+fdlg_name], 0
    mov [fdlg_nlen], bl
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_rows - the display row count (SPEC.md 38.4)
; in:  nothing
; out: AX = rows = [disk_nfiles] + the '..' row, CX = 1 when that row exists
;      (0 at the root). Preserves everything else.
; The ONE place the '..' offset is decided; every row <-> directory index
; conversion in this module goes through it.
; -----------------------------------------------------------------------------
fdlg_rows:
    xor cx, cx
    cmp word [dsk_cwd], 0
    je .root
    mov cx, 1
.root:
    mov ax, [disk_nfiles]
    add ax, cx
    ret

; -----------------------------------------------------------------------------
; fdlg_stage - stage one display row (SPEC.md 38.4)
; in:  AX = display row, already checked against fdlg_rows
; out: fdlg_row = its NUL name, [fdlg_type] / [fdlg_size] = the SPEC.md 19
;      type and size words. The '..' row is synthesized as type 2 so every
;      consumer treats it as the folder it is. Preserves all registers.
; -----------------------------------------------------------------------------
fdlg_stage:
    push ax
    push bx
    push cx
    push si
    push ax
    call fdlg_rows              ; CX = the '..' offset
    pop ax
    or cx, cx
    jz .entry
    or ax, ax
    jnz .below
    mov word [fdlg_type], 2
    mov word [fdlg_size], 0
    mov word [fdlg_row], '..'   ; two bytes, then the terminator
    mov byte [fdlg_row+2], 0
    jmp short .out
.below:
    dec ax                      ; display row -> directory index
.entry:
    call dsk_get_dir            ; SI -> the staged 32-byte entry (SPEC.md 19)
    mov bx, fdlg_row
    mov cx, 16
.copy:
    lodsb                       ; DF=0 on entry per SPEC.md 1
    mov [bx], al
    inc bx
    loop .copy
    mov byte [fdlg_row+16], 0
    mov ax, [si]                ; SI has advanced to offset 16: the type...
    mov [fdlg_type], ax
    mov ax, [si+4]              ; ...and offset 20: the size
    mov [fdlg_size], ax
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_go - navigate to a directory (SPEC.md 38.4)
; in:  AX = first cluster (0 = root), DL = drive; caller holds the gfx lock
; out: nothing; selection and scroll reset. Preserves all registers.
; A remount, exactly as the Disk window's own navigation is (SPEC.md 19.2):
; sixteen sector reads under the held lock, which stalls painters briefly
; and is the same trade SPEC.md 22 already accepted for Refresh. DL is an
; input rather than a read of [disk_drive] for disk_mount's own reason: it
; is the routine that WRITES [disk_drive], so the drive button has nothing
; to set beforehand.
; -----------------------------------------------------------------------------
fdlg_go:
    push ax
    call dsk_chdir
    mov word [fdlg_sel], 0xFFFF
    mov word [fdlg_scrl], 0
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_setsel - move the selection and scroll it into view (internal)
; in:  AX = desired display row (clamped to 0 .. rows-1; an empty listing
;      clears the selection instead)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_setsel:
    push ax
    push bx
    push cx
    push dx
    push ax
    call fdlg_rows              ; AX = total rows
    mov bx, ax
    pop ax
    or bx, bx
    jz .none
    or ax, ax
    jns .lo
    xor ax, ax
.lo:
    cmp ax, bx
    jb .clamped
    mov ax, bx
    dec ax
.clamped:
    mov [fdlg_sel], ax
    mov cx, [fdlg_scrl]
    cmp ax, cx
    jb .above
    mov dx, cx
    add dx, FD_ROWS
    cmp ax, dx
    jb .out                     ; already visible
    mov cx, ax
    sub cx, FD_ROWS-1
    jmp short .store
.above:
    mov cx, ax
.store:
    mov [fdlg_scrl], cx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.none:
    mov word [fdlg_sel], 0xFFFF
    jmp short .out

; -----------------------------------------------------------------------------
; fdlg_clamp - keep the scroll position inside the listing (internal)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_clamp:
    push ax
    push cx
    cmp word [fdlg_scrl], 0x8000    ; a decrement below zero wrapped: the
    jb .top                         ; floor is tested FIRST, or the wrapped
    mov word [fdlg_scrl], 0         ; value would clamp to the ceiling and
.top:                               ; the up-arrow would jump to the end
    call fdlg_rows              ; AX = total rows
    sub ax, FD_ROWS
    jg .cap
    xor ax, ax
.cap:
    cmp [fdlg_scrl], ax
    jbe .out
    mov [fdlg_scrl], ax
.out:
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_thumb - scroll-bar thumb geometry, shared by painter and hit-tester
; in:  nothing
; out: CF = 1 = no thumb (everything fits); else CF = 0, AX = thumb top and
;      DX = thumb height, both content-relative. Preserves everything else.
; -----------------------------------------------------------------------------
fdlg_thumb:
    push bx
    push cx
    call fdlg_rows              ; AX = total rows
    cmp ax, FD_ROWS
    jbe .none
    mov bx, ax                  ; BX = total
    mov ax, FD_ROWS * FD_TRKH
    xor dx, dx
    div bx                      ; AX = proportional thumb height
    cmp ax, 8
    jae .hok
    mov ax, 8
.hok:
    mov cx, ax                  ; CX = height
    mov ax, [fdlg_scrl]
    mov dx, FD_TRKH
    mul dx
    div bx                      ; AX = offset down the track
    add ax, FD_TRKY1
    mov dx, FD_TRKY1+FD_TRKH    ; clamp: top <= track end - height
    sub dx, cx
    cmp ax, dx
    jle .tok
    mov ax, dx
.tok:
    mov dx, cx
    pop cx
    pop bx
    clc
    ret
.none:
    pop cx
    pop bx
    stc
    ret

; =============================================================================
; Drawing. Everything below is content-relative and goes through fdlg_off /
; fdlg_text, which add the live content origin cached by fdlg_paint - the
; window can be dragged, so nothing may bake in FD_X/FD_Y.
; =============================================================================

; -----------------------------------------------------------------------------
; fdlg_off - content-relative rect to absolute (internal)
; in:  AX = x1, BX = y1, CX = x2, DX = y2
; out: the same four, offset by the content origin
; -----------------------------------------------------------------------------
fdlg_off:
    add ax, [fdlg_cx]
    add cx, [fdlg_cx]
    add bx, [fdlg_cy]
    add dx, [fdlg_cy]
    ret

; -----------------------------------------------------------------------------
; fdlg_text - draw a string at a content-relative point (internal)
; in:  CX = x, DX = y, SI = NUL string, [gfx_color] set
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_text:
    push cx
    push dx
    add cx, [fdlg_cx]
    add dx, [fdlg_cy]
    call font_str
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; fdlg_btn - one button of the right-hand column (internal)
; in:  BX = its top (content-relative), SI = label, AL non-zero = the
;      default button (a second frame 2px out)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    mov [fdlg_bdef], al         ; AL is about to become a rect corner
    mov bp, bx                  ; BP = the button top (a value, never an
                                ; address: SS is not DS, SPEC.md 1)
    mov byte [gfx_color], CBLACK
    mov ax, FD_BX1
    mov bx, bp
    mov cx, FD_BX2
    mov dx, bp
    add dx, FD_BH
    call fdlg_off
    call gfx_frame
    cmp byte [fdlg_bdef], 0
    je .label
    mov ax, FD_BX1-2
    mov bx, bp
    dec bx
    dec bx
    mov cx, FD_BX2+2
    mov dx, bp
    add dx, FD_BH+2
    call fdlg_off
    call gfx_frame
.label:
    call font_width             ; AX = 8 * length
    mov cx, FD_BX2-FD_BX1+1
    sub cx, ax
    shr cx, 1
    add cx, FD_BX1              ; centered in the 63px rect
    mov dx, bp
    add dx, 3
    call fdlg_text
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_paint - W_PAINT (SPEC.md 38.3)
; in:  SI = window ptr; content already white-filled, gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call wm_content             ; AX = content left, DX = content top
    mov [fdlg_cx], ax
    mov [fdlg_cy], dx

    ; --- header: the drive, then what kind of place this is ---
    mov byte [gfx_color], CBLACK
    mov si, fdlg_s_drva
    cmp byte [disk_drive], 0
    je .drv
    mov si, fdlg_s_drvb
.drv:
    mov cx, 6
    mov dx, 6
    call fdlg_text
    mov si, fdlg_s_root
    cmp word [dsk_cwd], 0
    je .place
    mov si, fdlg_s_sub
.place:
    cmp byte [dsk_mntok], 0     ; a failed mount says so rather than showing
    jne .hdr                    ; an empty folder that is not empty
    mov si, fdlg_s_nodisk
.hdr:
    mov cx, 30
    mov dx, 6
    call fdlg_text

    ; --- the list frame ---
    mov ax, FD_LX1
    mov bx, FD_LY1
    mov cx, FD_LX2
    mov dx, FD_LY2
    call fdlg_off
    call gfx_frame

    call fdlg_clamp
    call fdlg_draw_rows
    call fdlg_draw_sbar

    ; --- the button column ---
    mov si, fdlg_s_open
    cmp byte [fdlg_mode], 0
    je .defbtn
    mov si, fdlg_s_save
.defbtn:
    mov bx, FD_BY0
    mov al, 1                   ; the default button carries the outer frame
    call fdlg_btn
    mov si, fdlg_s_cancel
    mov bx, FD_BY1
    xor al, al
    call fdlg_btn
    mov si, fdlg_s_drive
    mov bx, FD_BY2
    xor al, al
    call fdlg_btn

    ; --- the name box ---
    mov si, fdlg_s_lopen
    cmp byte [fdlg_mode], 0
    je .nlabel
    mov si, fdlg_s_lsave
.nlabel:
    mov byte [gfx_color], CBLACK
    mov cx, 6
    mov dx, FD_NTY
    call fdlg_text
    mov ax, FD_NBX1
    mov bx, FD_NBY1
    mov cx, FD_NBX2
    mov dx, FD_NBY2
    call fdlg_off
    call gfx_frame
    mov si, fdlg_name
    mov cx, FD_NTX
    mov dx, FD_NTY
    call fdlg_text
    cmp byte [fdlg_mode], 0
    je .done                    ; no caret in Open mode: there is nothing
                                ; to type there (SPEC.md 38.5)
    call font_width             ; AX = 8 * length; SI is still fdlg_name
    add ax, FD_NTX
    add ax, [fdlg_cx]           ; a 1px caret right after the text
    mov bx, FD_NTY
    add bx, [fdlg_cy]
    mov dx, FD_NTY+7
    add dx, [fdlg_cy]
    call gfx_vline              ; AX = x, BX = y1, DX = y2
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
; fdlg_draw_rows - the visible rows plus the selection bar (internal)
; in:  the layout cache; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_draw_rows:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call fdlg_rows              ; AX = total rows
    mov [fdlg_total], ax
    sub ax, [fdlg_scrl]         ; shown = min(total - scroll, FD_ROWS)
    jg .some
    xor ax, ax
.some:
    cmp ax, FD_ROWS
    jbe .shown
    mov ax, FD_ROWS
.shown:
    mov [fdlg_shown], ax

    xor di, di                  ; DI = visible row index
.row:
    cmp di, [fdlg_shown]
    jae .sel
    mov ax, FD_ROWH
    mul di
    add ax, FD_ROW0
    mov bp, ax                  ; BP = this row's top, content-relative
    mov ax, di
    add ax, [fdlg_scrl]
    call fdlg_stage             ; fdlg_row / fdlg_type / fdlg_size

    mov byte [gfx_color], CBLACK
    mov si, fdlg_row
    mov cx, 10
    mov dx, bp
    add dx, 4                   ; +4 centres an 8px glyph in the 16px band
    call fdlg_text

    mov si, fdlg_s_folder       ; the right-hand column: a size, or the
    cmp word [fdlg_type], 2     ; word 'folder' - a column of zeroes under
    je .right                   ; every directory would only read as one
    mov ax, [fdlg_size]
    push di
    mov di, fdlg_num
    call fm_utoa
    pop di
    mov si, fdlg_num
.right:
    call font_width             ; AX = the string's width
    mov cx, 196
    sub cx, ax                  ; right-aligned, clear of the scroll bar
    mov dx, bp
    add dx, 4
    call fdlg_text

    inc di
    jmp short .row

.sel:
    mov ax, [fdlg_sel]          ; the selection bar, when it is in view
    cmp ax, [fdlg_total]        ; (0FFFFh = none also fails this)
    jae .out
    sub ax, [fdlg_scrl]
    js .out
    cmp ax, [fdlg_shown]
    jae .out
    mov cx, FD_ROWH
    mul cx
    add ax, FD_ROW0
    mov bx, ax                  ; y1 = the row band's top
    mov dx, ax
    add dx, FD_ROWH-1
    mov ax, FD_LX1+1
    mov cx, FD_SBX-1
    call fdlg_off
    call gfx_xor_fill
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_draw_sbar - the scroll bar: rules, arrows, track, thumb (internal)
; in:  the layout cache; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_draw_sbar:
    push ax
    push bx
    push cx
    push dx
    push bp

    mov byte [gfx_color], CBLACK
    mov ax, FD_SBX              ; the bar's own frame, inside the list's
    mov bx, FD_LY1
    mov cx, FD_LX2
    mov dx, FD_LY2
    call fdlg_off
    call gfx_frame

    mov ax, FD_SBX              ; the two arrow-cell rules
    mov bx, FD_LX2
    mov dx, FD_LY1+11
    call fdlg_hline
    mov dx, FD_LY2-11
    call fdlg_hline

    mov bp, 0                   ; up arrow: five rows, widths 1..9
    mov dx, FD_LY1+3
.up:
    mov ax, FD_SBX+7
    sub ax, bp
    mov bx, FD_SBX+7
    add bx, bp
    call fdlg_hline
    inc dx
    inc bp
    cmp bp, 5
    jb .up

    mov bp, 4                   ; down arrow: the same, widths 9..1
    mov dx, FD_LY2-7
.dn:
    mov ax, FD_SBX+7
    sub ax, bp
    mov bx, FD_SBX+7
    add bx, bp
    call fdlg_hline
    inc dx
    dec bp
    jns .dn

    mov ax, FD_SBX+1            ; the track, 50% grey between the rules
    mov bx, FD_TRKY1
    mov cx, FD_LX2-1
    mov dx, FD_TRKY2
    call fdlg_off
    call gfx_fill_gray

    call fdlg_thumb             ; CF = 1: everything fits, no thumb
    jc .out
    mov bx, ax                  ; y1 = thumb top
    add dx, ax
    dec dx                      ; y2 = top + height - 1
    mov ax, FD_SBX+2
    mov cx, FD_LX2-2
    call fdlg_off
    mov byte [gfx_color], CWHITE
    call gfx_fill
    mov byte [gfx_color], CBLACK
    call gfx_frame
.out:
    pop bp
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_hline - a content-relative horizontal line (internal)
; in:  AX = x1, BX = x2, DX = y (content-relative), [gfx_color] set
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_hline:
    push ax
    push bx
    push dx
    add ax, [fdlg_cx]
    add bx, [fdlg_cx]
    add dx, [fdlg_cy]
    call gfx_hline
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_repaint - white-fill our own content, then redraw it (internal)
; in:  SI = window ptr; gfx lock held
; out: nothing (all registers preserved)
; The Note Pad idiom (SPEC.md 22): W_PAINT is handed a pre-filled content by
; wm_paint_all, but a self-initiated repaint has to fill it itself. The
; caption never changes here, so this is always content-only - unlike the
; Disk window, which retitles on navigation.
; -----------------------------------------------------------------------------
fdlg_repaint:
    push ax
    push bx
    push cx
    push dx
    mov bx, si
    call wm_content             ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                   ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                   ; DX = y2
    pop bx                      ; BX = y1
    mov byte [gfx_color], CWHITE
    call gfx_fill
    call fdlg_paint             ; SI is still the window
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_act - the default action: the Open/Save button, or Enter
; in:  SI = window ptr; gfx lock held
; out: CF = 1 = the dialog is GONE (committed) and the caller must not touch
;      the window again; CF = 0 = still up and already repainted.
;
; Open mode acts on the SELECTION - a folder is entered, a file is the
; answer. Save mode always commits what is in the box, because a Save button
; that entered a folder instead of saving would be the single most
; surprising control in the system; folders are entered by double-clicking
; them, which is what the Standard File dialog this follows did too.
; -----------------------------------------------------------------------------
fdlg_act:
    push ax
    cmp byte [fdlg_mode], 0
    jne .save
    mov ax, [fdlg_sel]
    cmp ax, 0xFFFF
    je .refuse                  ; nothing chosen: the button is inert
    call fdlg_stage
    cmp word [fdlg_type], 2
    jne .save                   ; a file: commit it like Save does
    call fdlg_dive
    jmp short .stay
.save:
    cmp byte [fdlg_nlen], 0
    je .refuse                  ; an empty name is not an answer
    pop ax
    call fdlg_commit
    stc
    ret
.refuse:
    call snd_beep
.stay:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; fdlg_dive - enter the staged row's directory and repaint (internal)
; in:  fdlg_row / fdlg_type staged and type 2, SI = window ptr; lock held
; out: nothing (all registers preserved)
; The '..' row walks the on-disk link (SPEC.md 19.2); every other folder
; goes straight to its first cluster. A '..' that cannot be read leaves us
; where we are, which is the Disk window's answer too.
; -----------------------------------------------------------------------------
fdlg_dive:
    push ax
    push cx
    push dx
    cmp byte [fdlg_row], '.'
    jne .down
    call dsk_dotdot             ; AX = the parent's first cluster
    jc .out                     ; a missing or corrupt '..': stay put
    jmp short .go
.down:
    push si
    call fdlg_rows              ; CX = the '..' offset
    mov ax, [fdlg_sel]
    sub ax, cx                  ; display row -> directory index
    call dsk_get_dir            ; SI -> the staged entry
    mov ax, [si+18]             ; its first cluster (SPEC.md 19)
    pop si
.go:
    mov dl, [disk_drive]
    call fdlg_go
.out:
    call fdlg_repaint
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_onkey - W_ONKEY (SPEC.md 38.5)
; in:  AL = ascii, AH = scan, SI = window ptr; gfx lock held
; out: nothing (all registers preserved)
;
; Open mode drives the list; Save mode drives the name box, and the two
; share Enter, Esc and the four scroll keys. Every character in Save mode
; goes through diskw.inc's own dskw_char, so the box shows exactly the bytes
; that will be stored - the SPEC.md 22 rule, for the same reason.
; -----------------------------------------------------------------------------
fdlg_onkey:
    push ax
    push bx
    push cx
    push dx
    push di

    cmp al, 27
    je .cancel
    cmp al, 13
    je .enter
    cmp ah, 0x48                ; Up
    je .kup
    cmp ah, 0x50                ; Down
    je .kdn
    cmp ah, 0x49                ; PgUp
    je .kpgup
    cmp ah, 0x51                ; PgDn
    je .kpgdn
    cmp byte [fdlg_mode], 0
    je .out                     ; Open mode: nothing else is typing
    cmp al, 8
    je .bksp
    cmp al, 32
    jb .out

    mov bl, [fdlg_nlen]
    xor bh, bh
    cmp bl, FD_NAMEMAX
    jae .out
    cmp al, '.'
    jne .plain
    or bl, bl
    jz .out                     ; a short name may not start with the dot...
    call fdlg_hasdot
    jc .out                     ; ...and may not carry a second one
    jmp short .store
.plain:
    call dskw_char              ; upper-cases, CF = 1 = not in the FAT set
    jc .out
.store:
    mov [bx+fdlg_name], al
    inc bx
    mov byte [bx+fdlg_name], 0
    mov [fdlg_nlen], bl
    jmp short .redraw

.bksp:
    mov bl, [fdlg_nlen]
    xor bh, bh
    or bl, bl
    jz .out
    dec bl
    mov [fdlg_nlen], bl
    mov byte [bx+fdlg_name], 0  ; BX is the NEW length: the terminator lands
    jmp short .redraw           ; on the character just dropped

.kup:
    mov ax, [fdlg_sel]
    dec ax
    jmp short .move
.kdn:
    mov ax, [fdlg_sel]
    inc ax
    jmp short .move
.kpgup:
    mov ax, [fdlg_sel]
    sub ax, FD_ROWS
    jmp short .move
.kpgdn:
    mov ax, [fdlg_sel]
    add ax, FD_ROWS
.move:
    cmp word [fdlg_sel], 0xFFFF
    jne .setsel
    xor ax, ax                  ; no selection yet: any key lands on row 0
.setsel:
    call fdlg_setsel
    call fdlg_pick              ; keep the name box on the selection
    jmp short .redraw

.enter:
    call fdlg_act               ; CF = 1: the window is gone, do not repaint
    jmp short .out
.cancel:
    call fdlg_close
    jmp short .out
.redraw:
    call fdlg_repaint
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_hasdot - does the name being typed already carry a '.'? (internal)
; in:  nothing
; out: CF = 1 = yes. Preserves all registers.
; -----------------------------------------------------------------------------
fdlg_hasdot:
    push ax
    push si
    mov si, fdlg_name
.scan:
    mov al, [si]
    or al, al
    jz .no
    inc si
    cmp al, '.'
    jne .scan
    pop si
    pop ax
    stc
    ret
.no:
    pop si
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; fdlg_pick - put the selected FILE's name in the box (internal)
; in:  nothing
; out: nothing (all registers preserved)
; Selecting a folder leaves the box alone: the name in it is the answer the
; dialog will give, and a folder is never that.
; -----------------------------------------------------------------------------
fdlg_pick:
    push ax
    push si
    mov ax, [fdlg_sel]
    cmp ax, 0xFFFF
    je .out
    call fdlg_stage
    cmp word [fdlg_type], 2
    je .out
    mov si, fdlg_row
    call fdlg_setname
.out:
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_onclick - W_ONCLICK (SPEC.md 38.3)
; in:  CX = x, DX = y (absolute), SI = window ptr; gfx lock held
; out: nothing (all registers preserved)
; Test order: buttons, then the scroll bar, then the rows - the SPEC.md 22
; order, and for the same reason: the bar overlaps the row band's right edge
; and has to win there.
; -----------------------------------------------------------------------------
fdlg_onclick:
    push ax
    push bx
    push cx
    push dx
    push di

    mov bx, si
    push cx                     ; save the click x across wm_content
    mov cx, dx                  ; CX = click y
    call wm_content             ; AX = content left, DX = content top
    mov [fdlg_cx], ax
    mov [fdlg_cy], dx
    sub cx, dx                  ; CX = content-relative y
    pop dx
    sub dx, ax                  ; DX = content-relative x

    ; --- the button column ---
    cmp dx, FD_BX1
    jl .sbar
    cmp dx, FD_BX2
    jg .sbar
    cmp cx, FD_BY0
    jl .sbar
    cmp cx, FD_BY0+FD_BH
    jg .b_cancel
    call fdlg_act               ; the default button; CF = 1 = window gone
    jmp .out
.b_cancel:
    cmp cx, FD_BY1
    jl .out
    cmp cx, FD_BY1+FD_BH
    jg .b_drive
    call fdlg_close
    jmp .out
.b_drive:
    cmp cx, FD_BY2
    jl .out
    cmp cx, FD_BY2+FD_BH
    jg .out
    mov dl, [disk_drive]        ; the other drive, at its root; disk_mount
    xor dl, 1                   ; is what publishes [disk_drive]
    xor ax, ax
    call fdlg_go
    jmp .redraw

    ; --- the scroll bar ---
.sbar:
    cmp dx, FD_SBX
    jl .rows
    cmp dx, FD_LX2
    jg .out
    cmp cx, FD_LY1
    jl .out
    cmp cx, FD_LY2
    jg .out
    cmp cx, FD_LY1+11
    jle .s_up
    cmp cx, FD_LY2-11
    jge .s_dn
    call fdlg_thumb             ; the track pages against the thumb
    jc .redraw                  ; no thumb: the track is inert
    cmp cx, ax
    jl .s_pgup
    add ax, dx
    cmp cx, ax
    jge .s_pgdn
    jmp short .redraw           ; on the thumb itself: nothing to do
.s_up:
    dec word [fdlg_scrl]
    jmp short .redraw
.s_dn:
    inc word [fdlg_scrl]
    jmp short .redraw
.s_pgup:
    sub word [fdlg_scrl], FD_ROWS
    jmp short .redraw
.s_pgdn:
    add word [fdlg_scrl], FD_ROWS
    jmp short .redraw

    ; --- the rows ---
.rows:
    cmp dx, FD_LX1
    jl .out
    cmp cx, FD_ROW0
    jl .out
    mov ax, cx
    sub ax, FD_ROW0
    mov cl, 4
    shr ax, cl                  ; / FD_ROWH
    cmp ax, FD_ROWS
    jae .out
    add ax, [fdlg_scrl]
    push ax
    call fdlg_rows              ; AX = total rows
    mov bx, ax
    pop ax
    cmp ax, bx
    jae .out                    ; past the last row: leave the selection

    cmp ax, [fdlg_sel]          ; a second click on the selected row?
    jne .select
    mov bx, [ui_click_t]        ; the click's BIRTH tick, not [ticks]:
    sub bx, [fdlg_clkt]         ; wrap-safe, and queued clicks must not
    cmp bx, FD_DBLCLK           ; collapse into double-clicks (SPEC.md 22)
    jae .select
    call fdlg_stage
    cmp word [fdlg_type], 2
    je .dive
    cmp byte [fdlg_mode], 0     ; a double-clicked FILE: in Open mode that
    jne .select                 ; is the answer; in Save mode it is only a
    call fdlg_commit            ; name for the box, which .select just put
    jmp short .out              ; there
.dive:
    call fdlg_dive              ; repaints itself
    jmp short .out

.select:
    call fdlg_setsel
    mov bx, [ui_click_t]
    mov [fdlg_clkt], bx
    call fdlg_pick
.redraw:
    call fdlg_repaint           ; SI is still the window ptr
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

                                ; (fdlg_win, the gate, is NOT here - it is a
                                ; `dw 0` up in .text; see the note there)
fdlg_mode:  resb 1              ; 0 = Open, 1 = Save
fdlg_rqwin: resw 1              ; the requester's window...
fdlg_rqcb:  resw 1              ; ...its completion proc OFFSET (the segment
                                ; is resolved at invoke time, SPEC.md 38.6)
fdlg_rqrec: resw 1              ; ...its instance record...
fdlg_rqsp:  resw 1              ; ...and that record's I_SPTR (a segment
                                ; since v3, compared only for equality): the
                                ; staleness triple of SPEC.md 38.6
fdlg_sel:   resw 1              ; selected DISPLAY row, 0FFFFh = none
fdlg_scrl:  resw 1              ; first visible row
fdlg_clkt:  resw 1              ; birth tick of the last click on the
                                ; selected row (the double-click window)
fdlg_name:  resb 16             ; the answer: the caller's default in, the
                                ; chosen name out, the edit buffer between
fdlg_nlen:  resb 1              ; its length, 0..12
fdlg_cx:    resw 1              ; content origin, cached by paint and click
fdlg_cy:    resw 1
fdlg_total: resw 1              ; display rows, and how many of them show -
fdlg_shown: resw 1              ; painter scratch, valid inside one draw
fdlg_row:   resb 18             ; one staged row's name (16 + NUL, padded)
fdlg_type:  resw 1              ; and its SPEC.md 19 type...
fdlg_size:  resw 1              ; ...and size words
fdlg_num:   resb 8              ; the size column's digits
fdlg_bdef:  resb 1              ; fdlg_btn: is this the default button?

section .text
