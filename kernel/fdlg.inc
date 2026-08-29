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
;
; **This module is `.cold` (SPEC.md 2.6).** Its code has a CS of its own, so
; a routine the kernel reaches from outside is a six-byte resident thunk in
; kernel.asm under the PUBLIC name, and the body here carries an `_x` suffix;
; calls that leave the cold set go out through `KERNEL_SEG:cw_*`. All of this
; module's data is above the single section toggle, which is the tidiest of
; the five - but the rule is the same, and a `db`/`dw` added below it is read
; at the wrong segment with no diagnostic. Nor may anything take a kernel
; segment from CS, which is COLD_SEG here.
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
FD_ICOX     equ 10              ; the row's 16x16 icon (SPEC.md 54.1) - it
FD_TEXTX    equ 28              ; fits because FD_ROWH is already 16, and the
                                ; name clears it by 2. The margin after that
                                ; is EIGHT PIXELS and no more: a 12-character
                                ; name (FD_NAMEMAX) ends at 124, and the
                                ; largest size that can exist is 8 digits -
                                ; BPB rule 8 caps a volume at 65,535 sectors
                                ; - which right-aligned at 196 starts at 132.
                                ; A wider icon has to move the size column
                                ; (FD_TRKY1/FD_TRKY2/FD_TRKH were here. The
                                ; track is the SHARED element's now - SPEC.md
                                ; 13.10 derives it from the bar's own rect, so
                                ; two arrow cells a pixel deeper than files
                                ; .inc's stopped being a thing this file could
                                ; be wrong about on its own)

FD_BX1      equ 224             ; the button column, 62 x 14 each
FD_BX2      equ 286
FD_BY0      equ 20              ; Open / Save (the default button)
FD_BY1      equ 40              ; Cancel
FD_BY2      equ 60              ; Drive
FD_BY3      equ 80              ; New Folder - SAVE MODE ONLY (SPEC.md 38.3)
FD_BH       equ 13
FD_BH2      equ 24              ; ...and it is TWO lines tall: 'New' over
                                ; 'Folder' says what the button does, where
                                ; 'New' alone next to Save/Cancel/Drive reads
                                ; as new WHAT. 63px of button holds 7 glyphs,
                                ; so the two words cannot share a line
FD_BT1      equ 4               ; the two text rows inside it, 8px glyphs
FD_BT2      equ 13              ; with 3px of margin above and below

FD_NBX1     equ 72              ; the name box
FD_NBX2     equ 213
FD_NBY1     equ 124
FD_NBY2     equ 140
FD_NTX      equ 76              ; text inside it
FD_NTY      equ 128

FD_NAMEMAX  equ 12              ; 8 + '.' + 3, the whole 8.3 budget
FD_DBLCLK   equ 9               ; double-click window, ticks (as SPEC.md 22)

%if FD_NAMEMAX + 1 > INST_FNSZ  ; the per-instance name row (SPEC.md 38.10)
  %error "INST_FNSZ too small for FD_NAMEMAX + NUL"
%endif

; -----------------------------------------------------------------------------
; fdlg_tpl - the window template (SPEC.md 11): x, y, w, h, title, paint,
; onkey, onclick. The title word is overwritten per mode by fdlg_open.
; -----------------------------------------------------------------------------
fdlg_tpl:   dw FD_X, FD_Y, FD_W, FD_H
            dw fdlg_s_topen, fdlg_paint, fdlg_onkey, fdlg_onclick

fdlg_s_topen:  db 'Open', 0
fdlg_s_tsave:  db 'Save As', 0
fdlg_s_media:  db 'MEDIA', 0     ; the default location (SPEC.md 38.10).
                                 ; UP HERE with the rest of this module's
                                 ; data, and not beside fdlg_home_go, because
                                 ; everything below the `section .cold`
                                 ; toggle is addressed through CS - and
                                 ; dsk_find_name reads its name through DS,
                                 ; so a string down there is read out of the
                                 ; cold segment's code. It assembles, and the
                                 ; folder is simply never found
; THE PAPER fdlg_text LETTERS ON. A constant while every caller drew on the
; dialog's white pane; fdlg_btn2's PRESSED button fills its interior CBLACK and
; then letters in CWHITE, so ink and paper were both white and font_run laid a
; solid white block over it - press-and-hold on New/Folder painted two bars
; where the captions should be. In `.text` and not `.bss`: this is `nasm -f
; bin` and .bss is not zeroed.
fdlg_paper:    db CWHITE
fdlg_s_open:   db 'Open', 0
fdlg_s_save:   db 'Save', 0
fdlg_s_cancel: db 'Cancel', 0
fdlg_s_drive:  db 'Drive', 0
fdlg_s_newf:   db 'New', 0      ; the two-line New Folder button (FD_BH2)
fdlg_s_newf2:  db 'Folder', 0
fdlg_s_lopen:  db 'Open:', 0
fdlg_s_lsave:  db 'Save as:', 0
fdlg_s_lfold:  db 'Folder:', 0 ; 8 glyphs is what fits before the box
fdlg_s_folder: db 'folder', 0
fdlg_s_up:     db 'up', 0
fdlg_s_root:   db 'Disk', 0
fdlg_s_sub:    db 'Folder', 0
fdlg_s_nodisk: db 'no disk', 0
fdlg_s_drv:    db 'A:', 0        ; the letter is STAMPED per paint (SPEC.md
                                 ; 38.11) - 'A' + [disk_drive], the same
                                 ; derivation desk_zone_label and the Disk
                                 ; window use. It was two strings picked by
                                 ; `cmp byte [disk_drive], 0`, so every
                                 ; volume above A: drew as 'B:' - and on a
                                 ; machine with a hard disk that is a header
                                 ; naming the wrong volume over a listing
                                 ; that is right

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

section .cold                   ; cold code (SPEC.md 2.6): resident, but in a
                                ; segment of its own, so it costs KERN_CODE_MAX
                                ; nothing. DS is still KERNEL_SEG, so every data
                                ; reference below is unchanged - and this module's
                                ; data is all ABOVE, so there is one toggle


; -----------------------------------------------------------------------------
; fdlg_open - API slot 0x0150 (SPEC.md 38.6)
; in:  AL = 0 Open / 1 Save, BX = requester window ptr, DI = completion proc
;      (near, in the caller's image), SI = default name (NUL, <= 12) or 0.
;      THE CALLER HOLDS THE GFX LOCK - every legal caller is a window
;      callback or an AM_ONCMD handler, which is what lets this create and
;      show the window inline instead of posting a launch.
; out: CF = 0 accepted and the dialog is already on screen; CF = 1 refused
;      (one is already up, BX is not a live owned window, or the window
;      table is full). All registers preserved.
;
; The staleness triple (record, I_WIN, I_SPTR) is captured here and checked
; again at commit time: a package whose window closes while the dialog is up
; has had its region freed, and its near callback pointer stops meaning
; anything. The window pointer alone would not do - window slots are reused.
; -----------------------------------------------------------------------------
fdlg_open_x:
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

    call KERNEL_SEG:cw_inst_win_owner ; DI = the owning record, or 0; BX preserved
    or di, di
    jz .refuse
    cmp byte [di+I_STATE], 1
    jne .refuse
    mov [fdlg_rqrec], di
    mov ax, [di+I_SPTR]
    mov [fdlg_rqsp], ax

    mov byte [fdlg_nlen], 0     ; seed the name box from the caller's default
    mov byte [fdlg_ncur], 0
    mov byte [fdlg_nmode], 0    ; ...as a FILE name; the New Folder button is
    mov byte [fdlg_name], 0     ; what switches the box to naming a folder
    or si, si
    jnz .setname                ; the CALLER's default wins - it knows which
    call fdlg_home_name         ; document it is on - and failing that, the
    jc .fresh                   ; name this app last picked (SPEC.md 38.10)
.setname:
    call fdlg_setname
.fresh:
    mov word [fdlg_sel], 0xFFFF
    mov word [fdlg_scrl], 0
    mov word [fdlg_clkt], 0

    call fdlg_home_go           ; open on THIS application's folder, not on
                                ; wherever the GLOBAL volume was last left by
                                ; a background write (SPEC.md 38.10) - and from
                                ; here until the dialog closes nothing else
                                ; can move it

    mov si, fdlg_tpl            ; wm_create reads the template at ES:SI, and
    push es                     ; ours is kernel data - which is also what
    push ds                     ; tells it this is a KERNEL window (W_SEG 0)
    pop es
    call KERNEL_SEG:cw_wm_create ; BX = window ptr, CF = table full
    pop es
    jc .refuse
    mov ax, fdlg_s_topen
    cmp byte [fdlg_mode], 0
    je .titled
    mov ax, fdlg_s_tsave
.titled:
    mov [bx+W_TITLE], ax
    mov word [fdlg_down], 0     ; a fresh dialog has nothing pressed
%ifdef KERN_BIG
    mov ax, fdlg_onup           ; the column fires on the RELEASE now (SPEC.md
    call KERNEL_SEG:cw_wm_onmouseup ; 13.8.3) and follows the pointer between
    mov ax, fdlg_ondrag         ; the two edges - both are .text thunks,
    call KERNEL_SEG:cw_wm_ondrag ; because this module is .cold and wm_pkgcall
                                ; dispatches a W_SEG-0 window NEAR
    mov ax, fdlg_s_topen        ; (AX was the title and is spent above; it is
    cmp byte [fdlg_mode], 0     ; rebuilt here rather than banked because the
    je .rt                      ; two stores below want it and a push/pop
    mov ax, fdlg_s_tsave        ; across two far calls is longer)
.rt:
    mov [bx+W_TITLE], ax
%endif
%ifdef WM_ANIM
    or word [bx+W_FLAGS], WF_NOANIM  ; SPEC.md 11.99.2: modal, and put up and
                                ; taken down on every Open and every Save
%endif
    mov [fdlg_win], bx          ; take the gate LAST: until this store the
    mov byte [ui_post], 1       ; ...and SAY SO once (SPEC.md 13.13)
                                ; window is an ordinary one
    call KERNEL_SEG:cw_wm_show  ; raise + repaint, under the caller's lock
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
; fdlg_hidx - the requesting instance's file-home row (internal, SPEC.md 38.10)
; in:  nothing ([fdlg_rqrec] names the record)
; out: CF = 0 with BX = the slot index; CF = 1 = no live requester, and every
;      caller then falls back to the global behaviour this module used to have
; clobbers: BX (the output), flags
;
; [fdlg_rqrec] is validated by fdlg_open before it is stored and again by
; fdlg_commit, so this only has to turn a record into a row - inst_fhome_idx
; owns that arithmetic, because the table is instance.inc's.
; -----------------------------------------------------------------------------
fdlg_hidx:
    push di
    mov di, [fdlg_rqrec]
    or di, di
    jz .no
    call KERNEL_SEG:cw_inst_fhome_idx ; BX = the slot
    cmp bx, INST_MAX
    jae .no
    pop di
    clc
    ret
.no:
    pop di
    stc
    ret

; -----------------------------------------------------------------------------
; fdlg_home_go - open on THIS application's folder (internal, SPEC.md 38.10)
; in:  nothing; the gfx lock is held (fdlg_open's contract)
; out: nothing (all registers preserved)
;
; This used to be an unconditional "re-list where the volume already is", and
; the volume is GLOBAL - so anything that moved it moved every application's
; idea of where its documents are. A Control Panel close writes SYSTEM.CFG to
; the system disk (SPEC.md 51.5.1) and a package writing a file leaves the
; volume wherever it wrote; either one silently relocated the next Save.
;
; Three answers, in this order:
;
;   the user has chosen a folder ->  back there (inst_fdrv/inst_fcwd)
;   they have not                ->  MEDIA in the root of THIS INSTANCE's
;                                    drive, or that root if there is no MEDIA
;   the volume will not mount    ->  the banked pair, which is the recovery
;
; **Nothing here reads the globals except as the recovery.** The drive comes
; out of the instance's own row (SPEC.md 19.2.1) and so does the folder, so a
; Disk window taken to C:, a background read, a driver's trip to A: for
; SYSTEM.CFG - none of them can decide where this application's Open lands.
; The instance's row is seeded at inst_alloc from wherever the app was
; launched, so even an app that has never opened a dialog has a drive of its
; own to resolve MEDIA on.
;
; [inst_fpick] is the middle row's gate and it is a question about the USER,
; not about the data: the row always holds a real folder, and this asks
; whether the person ever chose it. Until they have, the default wins.
;
; The banked pair is taken first and used last, because a remembered folder
; can have gone away - the disk was swapped, or the directory deleted - and
; dsk_chdir's failure path leaves [dsk_cwd] at 0 on the drive it tried, so
; re-reading the globals afterwards would not recover where the user is.
; -----------------------------------------------------------------------------
fdlg_home_go:
    push ax
    push bx
    push dx
    push si                     ; the MEDIA lookup below stages an entry
%ifdef KERN_BIG
    mov bx, fdlg_path           ; the dialog is being PLACED, so it has walked
    call pth_clear              ; nowhere yet (docs/TITLE-PLAN.md 5). Cleared
                                ; here rather than in fdlg_open_x because this
                                ; is what MOVES it, and .bss arrives holding
                                ; whatever the last dialog left
%endif
    mov al, [disk_drive]        ; the RECOVERY only: where the machine is now,
    mov [fdlg_hdrv], al         ; for the case where this instance's own
    mov ax, [dsk_cwd]           ; volume cannot be mounted at all
    mov [fdlg_hcwd], ax
    call fdlg_hidx
    jc .cur                     ; no live requester: the machine's position is
                                ; the only answer there is
    mov dl, [inst_fdrv+bx]      ; THIS instance's drive, for either branch
    cmp byte [inst_fpick+bx], 0
    je .media                   ; the user has chosen no folder for this app
    shl bx, 1                   ; ...otherwise the one they chose
    mov ax, [inst_fcwd+bx]
    call dsk_chdir_x
    jnc .done
.cur:
    mov dl, [fdlg_hdrv]
    mov ax, [fdlg_hcwd]
    call dsk_chdir_x
    jmp short .done

    ; --- the default: MEDIA in the root of this instance's drive ---
    ; Two mounts when it is there and one when it is not, paid on the opens
    ; before the user has chosen anywhere; after that the row above is taken
    ; and this never runs again for that instance.
.media:
    mov [fdlg_idrv], dl         ; banked: the two chdirs below both need it
    xor ax, ax                  ; the root, whose listing names its folders
    call dsk_chdir_x
    jc .cur                     ; the volume itself will not mount
    mov si, fdlg_s_media
    call dsk_find_name_x ; AX = its index; no I/O - the snapshot
    jc .done                    ; no MEDIA here: this root IS the answer, and
                                ; it is still an answer nothing drifted into
    call dsk_get_dir_x ; SI -> the staged entry (SPEC.md 19)
    cmp word [si+16], 2         ; a folder, not a FILE called MEDIA
    jb .done
    mov ax, [si+18]             ; its first cluster
    or ax, ax
    jz .done                    ; 0 is the root: not a folder to enter
    mov dl, [fdlg_idrv]
    call dsk_chdir_x
    jc .cur
%ifdef KERN_BIG
    mov bx, fdlg_path           ; the ONE default that can be named: the
    mov si, fdlg_s_media        ; dialog really did walk into MEDIA, and BX
    call pth_push               ; and SI are both saved by the prologue above
%endif
.done:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_home_name - the name this application last picked (SPEC.md 38.10)
; in:  nothing
; out: CF = 0 with SI -> a NUL-terminated name; CF = 1 = none recorded
; clobbers: SI (the output), flags
; -----------------------------------------------------------------------------
fdlg_home_name:
    push ax
    push bx
    push cx
    call fdlg_hidx
    jc .no
    mov al, bl
    mov cl, INST_FNSZ
    mul cl                      ; AX = slot * INST_FNSZ
    mov si, inst_fname
    add si, ax
    cmp byte [si], 0
    je .no
    pop cx
    pop bx
    pop ax
    clc
    ret
.no:
    pop cx
    pop bx
    pop ax
    stc
    ret

; -----------------------------------------------------------------------------
; fdlg_home_dir - record the folder this application is standing in (38.10)
; in:  the volume is where the user just navigated to
; out: nothing (all registers preserved; flags are not)
;
; Split out of fdlg_home_save because the FOLDER and the NAME are recorded at
; different moments and for different reasons. A name is only ever a name the
; user committed to; a folder is a place the user WENT, and going there is
; the statement - so this runs from fdlg_go on every navigation, cancelled
; dialog or not. Backing out of a folder is a navigation too, and records the
; folder backed out to.
;
; It reads the GLOBALS rather than fdlg_go's arguments, because what has to
; be recorded is where the mount actually landed.
;
; It writes the instance's OWN directory (SPEC.md 19.2.1) - the same pair its
; file operations resolve in - so a Save As does not merely aim the next
; dialog, it moves the application. [inst_fpick] is what it adds on top: the
; row already held a folder (seeded at launch), and this is the user saying
; they want THIS one.
; -----------------------------------------------------------------------------
fdlg_home_dir:
    push ax
    push bx
    push si
    call fdlg_hidx
    jc .out
    mov al, [disk_drive]
    mov [inst_fdrv+bx], al
    mov byte [inst_fpick+bx], 1 ; the user has now chosen, so the default
                                ; (SPEC.md 38.10) stops applying to this app
    mov si, bx
    shl si, 1
    mov ax, [dsk_cwd]
    mov [inst_fcwd+si], ax
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_home_save - record where this application ended up (SPEC.md 38.10)
; in:  the volume is at the chosen folder and fdlg_name holds the answer
; out: nothing (all registers preserved)
;
; The NAME is written on a COMMIT and nowhere else: a cancelled dialog is not
; a statement about which document an app is on. The folder beside it is
; fdlg_home_dir's, and is usually already recorded by the navigation that got
; here - this call is what covers the commit that never navigated at all.
;
; The copy is a plain byte loop rather than movsb because ES here is the
; requesting package's segment (SPEC.md 20.1) and stosb writes through it.
; -----------------------------------------------------------------------------
fdlg_home_save:
    push ax
    push bx
    push cx
    push si
    push di
    call fdlg_home_dir          ; the folder; BX below is the row again
    call fdlg_hidx
    jc .out
    mov al, bl
    mov cl, INST_FNSZ
    mul cl
    mov di, inst_fname
    add di, ax
    mov si, fdlg_name
    mov cx, INST_FNSZ - 1
.cp:
    mov al, [si]
    mov [di], al
    or al, al
    jz .out
    inc si
    inc di
    loop .cp
    mov byte [di], 0            ; a name that filled the row still terminates
.out:
    pop di
    pop si
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
    call KERNEL_SEG:cw_gfx_lock
    call fdlg_close
    call KERNEL_SEG:cw_gfx_unlock
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
fdlg_reap_x:
    cmp word [fdlg_win], 0
    je .out
    push ax
    push bx
    xor al, al                  ; no lock held: fdlg_gate takes its own
    call fdlg_gate
    pop bx
    pop ax
.out:
    retf

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
fdlg_grab_x:
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
    call KERNEL_SEG:cw_snd_beep
    stc
.out:
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; fdlg_top - the modal keyboard filter (SPEC.md 38.2)
; in:  BX = wm_top's answer; the caller HOLDS the gfx lock
; out: BX = the dialog window if one is up, else BX unchanged. Preserves
;      everything else.
; Belt to fdlg_grab's braces - nothing can be raised above a dialog while
; every click outside it is being swallowed - but ten bytes is a cheap price
; for a keystroke never reaching the window behind.
; -----------------------------------------------------------------------------
fdlg_top_x:
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
    retf

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
    call KERNEL_SEG:cw_wm_destroy ; and nothing may observe a gate pointing at
                                ; a freed record
    mov bx, [fdlg_rqwin]
    call KERNEL_SEG:cw_menu_activate
    call KERNEL_SEG:cw_menu_draw_bar
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
    call fdlg_home_save         ; this app's folder and name, for its NEXT
                                ; dialog (SPEC.md 38.10) - after the staleness
                                ; triple, so a dead record is never written to
    mov cx, [fdlg_rqcb]
    or cx, cx
    jz .out
    push word [snd_inst]        ; STAMP THE DISPATCHED INSTANCE (SPEC.md
    call KERNEL_SEG:cw_snd_disp_set ; 34.3), exactly as W_ONKEY, W_ONCLICK, the
                                ; menu handler and W_PAINT all do - and DI is
                                ; still [fdlg_rqrec] here, validated three
                                ; tests up, so the record is free.
                                ;
                                ; This was the one dispatch site that did not,
                                ; and the consequence was invisible: a sound
                                ; GRANT taken inside a completion proc got
                                ; snd_req_inst's fallback, the running task's
                                ; T_INST, which on the UI task is 0xFF - "no
                                ; instance". The app's own worker is stamped
                                ; with the real instance, so sbl_grant_chk's
                                ; owner compare could never match and every
                                ; stage into that grant was refused. Tracker
                                ; loads a module through this dialog and mixes
                                ; from a worker: the ring kept the open's
                                ; pre-roll and played zeros for the rest of
                                ; it, forever. The 0xFF stamp also outlives
                                ; the app, because snd_release_inst releases
                                ; by instance and nothing owns 0xFF.
    push es                     ; the callback owes us nothing (SPEC.md
    push bp                     ; 38.6), and ES is the one register the
    mov bp, cx                  ; kernel cannot shrug off (SPEC.md 1)
    call fdlg_sizeof            ; DX:CX = the chosen file's size, 0 if this
                                ; listing has no such file (a typed Save
                                ; name, a folder). A RAM lookup - no I/O.
    mov al, [fdlg_mode]         ; the callback's contract: AL = mode,
    mov ah, 0                   ; SI = its window, ES:DI = the name - ES,
    mov si, [fdlg_rqwin]        ; because for a package DS is its own
    mov di, fdlg_name           ; segment and this buffer is ours (20.1) -
                                ; and DX:CX = the SIZE, so an app can refuse
                                ; what it cannot fund before touching the
                                ; disk (SPEC.md 38.6)
    call KERNEL_SEG:cw_wm_pkgcall ; SI = its window, BP = the completion proc
    pop bp
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
; out: fdlg_name / [fdlg_nlen], truncated to FD_NAMEMAX. CF = 1 when the box
;      is not what it was - text, length or caret. Preserves all registers.
;
; The CF is what lets a click that lands on the row ALREADY selected draw
; nothing at all (SPEC.md 38.3): the box follows the selection, so the honest
; question is not "did the selection move" but "did the box", and only the
; setter can answer it. Typing into the box between the two clicks is the one
; case where it did.
; -----------------------------------------------------------------------------
fdlg_setname:
    push ax
    push bx
    push cx
    push si
    xor bx, bx
    xor cx, cx                  ; CX counts what came out different
.copy:
    mov al, [si]
    or al, al
    jz .done
    cmp bx, FD_NAMEMAX
    jae .done
    cmp al, [bx+fdlg_name]
    je .same
    inc cx
.same:
    mov [bx+fdlg_name], al
    inc bx
    inc si
    jmp short .copy
.done:
    cmp byte [bx+fdlg_name], 0  ; the terminator: a SHORTER name differs here
    je .tsame                   ; and nowhere above
    inc cx
.tsame:
    mov byte [bx+fdlg_name], 0
    cmp [fdlg_nlen], bl
    je .lsame
    inc cx
.lsame:
    mov [fdlg_nlen], bl
    cmp [fdlg_ncur], bl         ; a name arriving whole puts the caret after
    je .csame                   ; it, which is where typing continues - and
    inc cx                      ; moving the caret alone is a redraw
.csame:
    mov [fdlg_ncur], bl
    or cx, cx
    jz .nochange
    stc
    jmp short .out
.nochange:
    clc
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_rows - the display row count (SPEC.md 38.4)
; in:  nothing
; out: AX = [disk_nfiles]. Preserves everything else.
;
; A display row IS a directory index. It used not to be: this module
; synthesized its own '..' row and carried a +1 offset through every row <->
; index conversion in it. The MOUNT puts the parent link in the listing now
; (SPEC.md 19.5), so the dialog, the Disk window and every view cache get the
; same row from the same place - and the offset, and the one routine that
; existed to decide it, are gone.
; -----------------------------------------------------------------------------
fdlg_rows:
    mov ax, [disk_nfiles]
    ret

; -----------------------------------------------------------------------------
; fdlg_sizeof - how big is the file the box now names? (SPEC.md 38.6)
;
; in:  fdlg_name = the chosen name, NUL, as the callback will receive it
; out: DX:CX = its size in bytes, or 0:0 when this listing has no such FILE -
;      a name typed for a Save, a folder, or anything not found. All other
;      registers preserved.
;
; A LISTING lookup, not a directory search: the mount snapshot is already in
; RAM (SPEC.md 38.2 - modality is what lets this module read the global
; snapshot directly), so this costs no floppy I/O at all. That is the whole
; point of handing the size to the callback - an app can refuse a load it
; cannot fund WITHOUT the motor ever spinning up, where before it had to
; claim a buffer and let dskw_read walk the directory to be told FERR_BIG.
; -----------------------------------------------------------------------------
fdlg_sizeof:
    push ax
    push bx
    push si
    push di
    xor cx, cx                  ; the not-found answer
    xor dx, dx
    call fdlg_rows              ; AX = how many entries this listing has
    or ax, ax
    jz .out
    mov bx, ax
    xor ax, ax
.each:
    call fdlg_stage             ; fdlg_row / fdlg_type / fdlg_size
    cmp word [fdlg_type], 2     ; a folder has no size to report
    jae .next
    push ax
    mov si, fdlg_row            ; case-sensitive is right: both sides are the
    mov di, fdlg_name           ; mount's own 8.3 display form (SPEC.md 19)
.cmp:
    mov al, [si]
    cmp al, [di]
    jne .nomatch
    or al, al
    jz .match
    inc si
    inc di
    jmp short .cmp
.nomatch:
    pop ax
    jmp short .next
.match:
    pop ax
    mov cx, [fdlg_size]
    mov dx, [fdlg_sizeh]
    jmp short .out
.next:
    inc ax
    cmp ax, bx
    jb .each
.out:
    pop di
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_stage - stage one display row (SPEC.md 38.4)
; in:  AX = display row = directory index, already checked against fdlg_rows
; out: fdlg_row = its NUL name, [fdlg_type] / [fdlg_size] = the SPEC.md 19
;      type and size words. Preserves all registers.
; -----------------------------------------------------------------------------
fdlg_stage:
    push ax
    push bx
    push cx
    push si
    call dsk_get_dir_x ; SI -> the staged 32-byte entry (SPEC.md 19)
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
    mov ax, [si+4]              ; ...and offsets 20/22: the size dword
    mov [fdlg_size], ax
    mov ax, [si+6]
    mov [fdlg_sizeh], ax
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_nextvol - the live volume after the current one, wrapping (SPEC.md 38.11)
; in:  nothing
; out: DL = a live volume index - the current one if it is the only one
; clobbers: DL (the output), flags
;
; The Drive button was `xor dl, 1`, "the other drive": true while A: and B:
; were the whole machine, and false since SPEC.md 52, where [disk_drive] is a
; VOLUME INDEX and a disk driver mounts one per partition. No Open or Save in
; any application could reach a hard disk at all - the button cycled 0 <-> 1
; and the dialog has no other way to change volume.
;
; dsk_vtab is read STRAIGHT, with no cw_ shim: cold code keeps DS = KERNEL_SEG
; (SPEC.md 2.6), so kernel DATA is already reachable here and only CALLS have
; to cross - which is what keeps this fix out of .text entirely.
; -----------------------------------------------------------------------------
fdlg_nextvol:
    push bx
    push cx
    mov dl, [disk_drive]
    mov cl, DVOL_MAX            ; ...so the last candidate tried is where we
.next:                          ; started, and a machine with one volume
    inc dl                      ; answers it rather than running off the end
    cmp dl, DVOL_MAX
    jb .test
    xor dl, dl
.test:
    mov bl, dl
    mov bh, 0
    shl bx, 1
    shl bx, 1
    shl bx, 1
    shl bx, 1                   ; * DV_SIZE (16), the dsk_vol_slot shift
    cmp byte [dsk_vtab+bx+DV_KIND], DVK_FREE
    jne .out
    dec cl
    jnz .next
    mov dl, [disk_drive]        ; unreachable while A: and B: are permanent
.out:                           ; rows, and cheaper than trusting that
    pop cx
    pop bx
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
;
; **THIS is where the user changing directories is recorded** (SPEC.md 38.10).
; All three ways to move inside the dialog come through here - the folder
; dive, the drive button and the folder just created - and nothing else does:
; fdlg_home_go calls dsk_chdir itself precisely so that arriving at a default
; does not read as a choice. So one store here covers every user navigation
; and cannot cover anything else, which is the whole distinction the feature
; rests on.
; -----------------------------------------------------------------------------
fdlg_go:
    push ax
    call dsk_chdir_x
    jc .moved                   ; a FAILED mount lands at the root of the
    call fdlg_home_dir          ; drive it tried with the gate shut - not a
.moved:                         ; folder to bring an app back to
%ifdef KERN_BIG
    cmp word [dsk_cwd], 0       ; AT A ROOT - asked for, or fallen back to -
    jne .haspath                ; so the dialog is in nothing it walked into.
    push bx                     ; files.inc's rule in the other module: the
    mov bx, fdlg_path           ; ONE clear site, so a new way of ending at a
    call pth_clear              ; root cannot forget to
    pop bx
.haspath:
%endif
    mov word [fdlg_sel], 0xFFFF
    mov word [fdlg_scrl], 0
    call fdlg_unfold            ; a folder name half typed here would be made
    pop ax                      ; THERE - abandon it with the location it was
    ret                         ; meant for (fdlg_newfolder calls this itself,
                                ; and unfolds again after: both are no-ops the
                                ; second time)

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
; fdlg_icon - one listing entry's 16x16 icon (SPEC.md 54.1, internal)
; in:  AX = directory index, CX = x, DX = y (content-relative)
; out: nothing (all registers preserved)
;
; It cannot share files.inc's fm_draw_icon16, which reads the POSTING
; window's view cache through fmv_get_icon. This dialog is modal (SPEC.md
; 38.2), so nothing can navigate the volume while it is up and it reads the
; global mount snapshot directly - which is also why the document icons
; SPEC.md 54.1 composed at mount are simply there, with no cache to fill.
;
; Same all-zero fallback as every other viewer: the 32 words OR to 0 means
; the entry earned no icon, and the built-in ico_app16 stands in.
; -----------------------------------------------------------------------------
fdlg_icon:
    push ax
    push bx
    push cx
    push dx
    push si
    add cx, [fdlg_cx]           ; content-relative -> absolute
    add dx, [fdlg_cy]
    call dsk_get_icon_x ; SI -> the staged 64-byte body
    mov bx, si
    push cx
    xor ax, ax
    mov cx, 32
.orw:
    or ax, [bx]
    inc bx
    inc bx
    loop .orw
    pop cx
    or ax, ax
    jz .generic
    call KERNEL_SEG:cw_icon_draw16 ; a harvested, folder or composed body
    jmp short .out
.generic:
    mov si, ico_app16           ; nothing earned one: the built-in
    call KERNEL_SEG:cw_icon_draw
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_text - draw a string at a content-relative point (internal)
; in:  CX = x, DX = y, SI = NUL string, [gfx_color] set
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_text:
    push cx
    push dx
    push ax
    add cx, [fdlg_cx]
    add dx, [fdlg_cy]
    mov al, [gfx_color]         ; OPAQUE (SPEC.md 38.12), and the ink is read
    mov ah, [fdlg_paper]        ; back rather than passed: seven call sites
    call KERNEL_SEG:cw_font_run ; already set the pen and none had to change.
    pop ax                      ; The paper follows the GROUND - white for the
                                ; dialog's pane, whose SELECTION is 38.3's XOR
                                ; band drawn over the rows afterwards, and
                                ; black for the one caller that fills its own
                                ; ground first (fdlg_btn2's pressed button)
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; fdlg_actok - would the default button actually do anything? (internal)
; in:  nothing
; out: CF = 0 yes; CF = 1 it would refuse
; clobbers: nothing (flags)
;
; The predicate the greying, the click and the Enter key all share (SPEC.md 47
; rule 4) - it is `fdlg_act`'s own test, lifted, so the button cannot look
; available and refuse. A chosen ROW always acts: fdlg_stage puts its name in
; the box first, so open, dive and save-under-that-name all have something to
; work with. With no row chosen the box is the whole answer, and an empty box
; is not one.
; -----------------------------------------------------------------------------
fdlg_actok:
    cmp word [fdlg_sel], 0xFFFF
    jne .yes
    cmp byte [fdlg_nlen], 0
    je .no
.yes:
    clc
    ret
.no:
    stc
    ret

; =============================================================================
; The STANDARD CONTROL, shared with every package (apps/os88ui.inc).
;
; One source, two worlds: OS88UI_KERNEL turns its primitive calls into the
; cold-to-text shims below, and a package assembling the identical text gets
; OSAPI_* far calls instead. A feature added to the button lands here, in the
; Control Panel, in the Timer and in every package at once - which is the
; whole reason it is one file rather than ten that agree by hand.
;
; It goes in fdlg.inc because this is the biggest consumer and the file is
; cold in its entirety; ctrl.inc is cold too and calls it directly, and
; apps.inc is .text and reaches it through os88ui_btn_f.
; =============================================================================
%ifdef OS88UI_SBDRAG
; THE DIALOG'S THUMB-DRAG POLICY (SPEC.md 13.10.5.4), and it is the WINDOW's
; rather than the element's, exactly as the Disk window's is.
;
; RATE 0, and here the argument is stronger than the Disk window's: this
; dialog has no incremental scroll at all. Every change to fdlg_scrl repaints
; the whole list through fdlg_draw_list - a fill, FD_ROWS rows of filename and
; the bar entire - where files.inc at least has gfx_scroll and a rows-only
; tier. So a following view is a full list repaint per pointer movement, and
; PERFORMANCE.md prices a row of text in tens of milliseconds on the target.
%ifndef FD_SBRATE
%define FD_SBRATE 0             ; ticks between commits; 0 = the release
%endif                          ; commits and nothing before it
%endif

%define OS88UI_KERNEL
%define OS88UI_SCROLL          ; SPEC.md 13.10: both kernel bars use it
                               ; OS88UI_SBDRAG - SPEC.md 13.10.5's thumb drag -
                               ; is NOT defined here. It has to be resolved
                               ; before files.inc, which is included first;
                               ; kernel.asm carries it beside WM_ANIM and
                               ; OS88_THEME and says why
%include "os88ui.inc"

; -----------------------------------------------------------------------------
; fdlg_defbtn - draw the default button (Open / Save) in the right pen
; in:  the layout cache; gfx lock held
; out: nothing (all registers preserved); the pen is left LIVE by os88ui_btn
; -----------------------------------------------------------------------------
fdlg_defbtn:
    push ax
    push bx
    push si
    mov si, fdlg_s_open
    cmp byte [fdlg_mode], 0
    je .lbl
    mov si, fdlg_s_save
.lbl:
    call fdlg_actok
    mov al, 0
    adc al, 0                   ; AL = 1 refused, 0 live - and note ADC WRITES
    mov [fdlg_actl], al         ; CF, so the carry the gate answered in is
    mov ah, al                  ; gone by here, which is why the answer is
                                ; carried as a VALUE from here on rather than
                                ; re-read from the flag (it used to be passed
                                ; to gfx_pen_cf and drew the button live every
                                ; time - the "looks available and refuses"
                                ; failure rule 4 exists to prevent)
    mov bx, FD_BY0
    mov al, OS88UI_DEF | OS88UI_FILL
    cmp word [fdlg_down], 1     ; ...and DOWN if it is the one being held
    jne .nodown                 ; (SPEC.md 13.8.3). Read here rather than
    or al, OS88UI_DOWN          ; passed, so fdlg_paint's column and
.nodown:                        ; fdlg_setdown's two edges cannot disagree -
                                ; and os88ui_btn settles the precedence, DIS
                                ; outranking DOWN, because a button that is
                                ; going to refuse must not say "pressed"
                                ; the outer ring - and the FILL, because this
                                ; is the one button in the kernel REDRAWN IN
                                ; PLACE (fdlg_draw_name, on the edge where
                                ; fdlg_actok moves). font_char is transparent,
                                ; so without an erase a checkerboard caption
                                ; is OR'd onto the solid one already there and
                                ; the greyed label comes out pixel-identical
                                ; to the live one - SPEC.md 47 rule 1 defeated
                                ; downstream of a mechanism that works
    call fdlg_btn               ; ...and AH carries the greying (SPEC.md 47 r1)
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_btn - one button of the right-hand column (internal)
; in:  BX = its top (content-relative), SI = label, AL = OS88UI_* flags
;      (DEF and/or FILL), AH non-zero = disabled
; out: nothing (all registers preserved); the pen is left LIVE
;
; The drawing is os88ui_btn's now - the shared control, one source with every
; package (apps/os88ui.inc). What is left here is the adapter: the column's
; fixed x, this button's y, and the window origin, turned into the screen rect
; the shared routine takes. AL is the shared routine's OWN flag word rather
; than a private encoding, so adding one is not two edits.
;
; The DISABLED state moved from the caller's pen to a flag, which is SPEC.md
; 47 rule 1 arriving properly: it used to be "set the pen, then call me", so
; the colour and [gfx_dis] were the caller's to get right and the label and
; the frame could disagree. Now one flag drives both and the pen comes back
; live by itself.
;
; Only the DEFAULT button passes FILL. Cancel, Drive and New Folder are drawn
; once, by fdlg_paint, onto a pane wm_paint_all has already whited - and
; nothing can change what they say - so an erase there is a gfx_fill nobody
; needs. The default button is the exception because its state moves.
; -----------------------------------------------------------------------------
fdlg_btn:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov di, ax                  ; DI = the two flag bytes; AX is about to be a
                                ; rect corner
    mov ax, FD_BX1
    mov cx, FD_BX2
    mov dx, bx
    add dx, FD_BH
    call fdlg_off               ; AX..DX = the inclusive SCREEN rect
    mov [os88ui_krect+0], ax
    mov [os88ui_krect+2], bx
    mov [os88ui_krect+4], cx
    mov [os88ui_krect+6], dx
    mov ax, di
    or ah, ah                   ; AL is already the flag word; the greying is
    jz .live                    ; the one bit that arrived separately
    or al, OS88UI_DIS
.live:
    xor ah, ah
    mov di, ax
    mov bx, os88ui_krect
    call os88ui_btn
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_btn2 - a TWO-LINE button of the same column (internal)
; in:  BX = its top (content-relative), SI = the upper label, DI = the lower
; out: nothing (all registers preserved)
;
; Never the default button, so there is no outer frame to draw and no AL to
; read. It exists for one button - New / Folder - because 'New' on its own,
; sitting under Save, Cancel and Drive, reads as new *what*, and the two
; words do not fit one 63px line.
; -----------------------------------------------------------------------------
fdlg_btn2:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx                  ; BP = the button top (a value, never an
                                ; address: SS is not DS, SPEC.md 1)
    mov byte [gfx_color], CWHITE ; the pressed look (SPEC.md 13.8), drawn rather
    test al, OS88UI_DOWN        ; than XOR-ed: interior black, both labels
    jz .bed                     ; white, same frame. It is written out here
    mov byte [gfx_color], CBLACK ; and not taken from os88ui_btn because this
.bed:                           ; is a TWO-LINE button and that routine centres
    push ax                     ; ONE label in both axes - but the FILL itself
    mov ax, FD_BX1              ; is unconditional, exactly as OS88UI_FILL is
    mov bx, bp                  ; on both of fdlg_setdown's edges: the ground
    mov cx, FD_BX2              ; under the one coming back UP is a pressed
    mov dx, bp                  ; button, and the captions are transparent, so
    add dx, FD_BH2              ; an up-draw with no erase leaves a black box
    call fdlg_off
    inc ax                      ; the interior, one pixel in from the frame
    inc bx
    dec cx
    dec dx
    call KERNEL_SEG:cw_gfx_fill
    pop ax
    mov byte [gfx_color], CBLACK
    push ax
    mov ax, FD_BX1
    mov bx, bp
    mov cx, FD_BX2
    mov dx, bp
    add dx, FD_BH2
    call fdlg_off
    call KERNEL_SEG:cw_gfx_frame
    pop ax
    test al, OS88UI_DOWN        ; ...and the two captions in white over it
    jz .ink
    mov byte [gfx_color], CWHITE
    mov byte [fdlg_paper], CBLACK   ; ...on the ground this branch just filled
.ink:
    mov dx, bp
    add dx, FD_BT1
    call fdlg_blabel            ; SI = the upper label
    mov si, di
    mov dx, bp
    add dx, FD_BT2
    call fdlg_blabel
    mov byte [fdlg_paper], CWHITE   ; UNCONDITIONALLY, so the up path cannot
                                    ; leave it black for the next caller
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_blabel - one line of button text, centred in the button column
; in:  SI = the string, DX = its top row (content-relative), [gfx_color] set
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_blabel:
    push ax
    push cx
    push si
    call KERNEL_SEG:cw_font_width ; AX = 8 * length
    mov cx, FD_BX2-FD_BX1+1
    sub cx, ax
    jns .pad                    ; a label wider than the button would make the
    xor cx, cx                  ; halved padding a huge unsigned number and
.pad:                           ; put the text off screen entirely
    shr cx, 1
    add cx, FD_BX1              ; centered in the 63px rect
    call fdlg_text
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_paint - W_PAINT (SPEC.md 38.3)
; in:  SI = window ptr; content already white-filled, gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_paint_x:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov bx, si
    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
    mov [fdlg_cx], ax
    mov [fdlg_cy], dx

    ; --- header: the drive, then what kind of place this is ---
    mov byte [gfx_color], CBLACK
    mov al, [disk_drive]
    add al, 'A'
    mov [fdlg_s_drv], al        ; the string is in .text, so it is writable -
    mov si, fdlg_s_drv          ; `fdlg_win` below is the same idiom
    mov cx, 6
    mov dx, 6
    call fdlg_text
    mov si, fdlg_s_root
    cmp word [dsk_cwd], 0
    je .place
%ifdef KERN_BIG
    push bx                     ; the folder's own name, when the dialog
    mov bx, fdlg_path           ; walked here (docs/TITLE-PLAN.md 5). The pen
    call pth_leaf               ; is x=30 and the list frame starts at
    pop bx                      ; FD_LX2=213, so 22 cells are free and the
    or si, si                   ; longest 8.3 name needs 12 - measured before
    jnz .place                  ; it was allowed here, not after
%endif
    mov si, fdlg_s_sub          ; ...and a folder it was PLACED in still reads
.place:                         ; 'Folder', which is what it always read
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
    call KERNEL_SEG:cw_gfx_frame

    call fdlg_list_body

    ; --- the button column, EVERY ONE THROUGH fdlg_btn1 -----------------
    ; ...which reads [fdlg_down] itself, so a repaint arriving in the middle
    ; of a gesture draws the held button held (SPEC.md 13.8.3). Open-coding
    ; the four here again is what would let the full paint and the two edges
    ; disagree about what is pressed.
    mov ax, 1
    call fdlg_btn1              ; Open / Save, greyed when it would refuse
    mov ax, 2
    call fdlg_btn1              ; Cancel
    mov ax, 3
    call fdlg_btn1              ; Drive
    cmp byte [fdlg_mode], 0     ; New Folder is a SAVE-mode button: opening a
    je .nonewf                  ; file has no use for one, and the fourth
    mov ax, 4                   ; frame would be clutter in the common case
    call fdlg_btn1
.nonewf:

    ; --- the name box ---
    mov si, fdlg_s_lopen
    cmp byte [fdlg_mode], 0
    je .nlabel
    mov si, fdlg_s_lsave
    cmp byte [fdlg_nmode], 0
    je .nlabel
    mov si, fdlg_s_lfold        ; the box is naming a folder: say so, or the
.nlabel:                        ; user is typing a folder name into a field
                                ; labelled 'Save as:'
    mov byte [gfx_color], CBLACK
    mov cx, 6
    mov dx, FD_NTY
    call fdlg_text
    mov ax, FD_NBX1
    mov bx, FD_NBY1
    mov cx, FD_NBX2
    mov dx, FD_NBY2
    call fdlg_off
    call KERNEL_SEG:cw_gfx_frame
    call fdlg_name_body

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Partial redraws - what a keystroke actually changes (SPEC.md 38.8)
;
; The dialog does not resize, so unlike a text window it never has to work out
; WHICH pixels moved: every key and every click knows what it touched. Typing
; a character changes the name box and nothing else - not the listing, not the
; four buttons, not the header, not two frames and a scroll bar - so a
; keystroke that repainted the whole content was redrawing about a hundred
; glyphs to move one.
;
; So the content is three parts, and each of the two that change on their own
; is a BODY (draws) plus a wrapper (erases its own rectangle first, and only
; its own). fdlg_paint calls the bodies, because wm_paint_all already handed
; it a white content; everything else calls the wrappers.
;
; The third part - the header, the two frames, the buttons and the name
; label - changes only when the volume, the mode or the folder-naming state
; does, and those paths keep fdlg_repaint. That is the rule for deciding
; where a new action goes: if it can change the drive letter, the word
; 'Disk'/'Folder', or the 'Save as:'/'Folder:' label, it is a full repaint.
; =============================================================================

; -----------------------------------------------------------------------------
; fdlg_name_body / fdlg_draw_name - the name box's contents, and the caret
; in:  [fdlg_cx]/[fdlg_cy] valid; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_draw_name:
    push ax
    push bx
    push cx
    push dx
    mov byte [gfx_color], CWHITE    ; INSIDE the frame: the frame itself is
    mov ax, FD_NBX1+1               ; part of the chrome and never moves
    mov bx, FD_NBY1+1
    mov cx, FD_NBX2-1
    mov dx, FD_NBY2-1
    call fdlg_off
    call KERNEL_SEG:cw_gfx_fill
    call fdlg_name_body
    call fdlg_actok             ; typing the first character, or deleting the
    mov al, 0                   ; last one, makes the default button live or
    adc al, 0                   ; dead - and this is the only path a keystroke
    cmp al, [fdlg_actl]         ; takes. Redrawn on the EDGE only: every caret
    je .out                     ; move comes through here too, and a frame
    call fdlg_defbtn            ; plus a label per keypress is real money on
.out:                           ; the machines this has to run on
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fdlg_name_body:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [gfx_color], CBLACK
    mov si, fdlg_name
    mov cx, FD_NTX
    mov dx, FD_NTY
    call fdlg_text
    cmp byte [fdlg_mode], 0
    je .out                     ; no caret in Open mode: there is nothing
                                ; to type there (SPEC.md 38.5)
    mov al, [fdlg_ncur]         ; a 1px caret AT the caret index - 8px cells,
    xor ah, ah                  ; so its column is the index times eight
    mov cl, 3
    shl ax, cl
    add ax, FD_NTX
    add ax, [fdlg_cx]
    mov bx, FD_NTY
    add bx, [fdlg_cy]
    mov dx, FD_NTY+7
    add dx, [fdlg_cy]
    call KERNEL_SEG:cw_gfx_vline ; AX = x, BX = y1, DX = y2
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_list_body / fdlg_draw_list - the listing and its scroll bar
; in:  [fdlg_cx]/[fdlg_cy] valid; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_draw_list:
    push ax
    push bx
    push cx
    push dx
    mov byte [gfx_color], CWHITE    ; inside the list frame, which the scroll
    mov ax, FD_LX1+1                ; bar's own rules are drawn against
    mov bx, FD_LY1+1
    mov cx, FD_LX2-1
    mov dx, FD_LY2-1
    call fdlg_off
    call KERNEL_SEG:cw_gfx_fill
    call fdlg_list_body
    pop dx
    pop cx
    pop bx
    pop ax
    ret

fdlg_list_body:
    call fdlg_clamp
    call fdlg_draw_rows
    call fdlg_draw_sbar
    ret

; -----------------------------------------------------------------------------
; fdlg_draw_both - a selection move: the listing scrolled AND the box took the
;                  new selection's name (fdlg_pick)
; in:  as above
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_draw_both:
    call fdlg_draw_list
    call fdlg_draw_name
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

    push ax                     ; the icon (SPEC.md 54.1): the index is
    mov ax, di                  ; recomputed rather than carried, so nothing
    add ax, [fdlg_scrl]         ; here depends on what fdlg_stage does to AX
    mov cx, FD_ICOX
    mov dx, bp
    call fdlg_icon
    pop ax

    mov byte [gfx_color], CBLACK
    mov si, fdlg_row
    mov cx, FD_TEXTX
    mov dx, bp
    add dx, 4                   ; +4 centres an 8px glyph in the 16px band
    call fdlg_text

    mov si, fdlg_s_folder       ; the right-hand column: a size, or the
    cmp word [fdlg_type], 2     ; word 'folder' - a column of zeroes under
    jae .right                  ; every directory would only read as one
    mov ax, [fdlg_size]
    mov dx, [fdlg_sizeh]
    push di
    mov di, fdlg_num
    call fm_ultoa_x             ; the full dword (SPEC.md 19)
    pop di
    mov si, fdlg_num
.right:
    call KERNEL_SEG:cw_font_width ; AX = the string's width
    mov cx, 196
    sub cx, ax                  ; right-aligned, clear of the scroll bar
    mov dx, bp
    add dx, 4
    call fdlg_text

    inc di
    jmp short .row

.sel:
    mov ax, [fdlg_sel]          ; the selection bar, drawn by the same proc a
    call fdlg_sel_bar           ; selection CHANGE uses (SPEC.md 22.2/38.3)
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
; fdlg_sel_bar - XOR the selection band for ONE display row
; in:  AX = display row, 0FFFFh = none; [fdlg_cx]/[fdlg_cy] valid; gfx lock
;      held
; out: nothing (all registers preserved)
;
; The Disk window's fm_sel_bar, in this module's geometry and for the same
; reason (SPEC.md 22.2): XOR means the call that puts the band up is the call
; that takes it down, so moving the selection costs two inverted strips
; instead of refilling the list. Range-checked here rather than at the call
; sites, because one of them hands it a row it has not looked at.
;
; It asks fdlg_rows for the total instead of reading [fdlg_shown], which is
; painter scratch and means nothing on the click path.
; -----------------------------------------------------------------------------
fdlg_sel_bar:
    push ax
    push bx
    push cx
    push dx
    push ax
    call fdlg_rows              ; AX = total display rows
    mov bx, ax
    pop ax
    cmp ax, bx                  ; 0FFFFh (none) also fails this
    jae .out
    sub ax, [fdlg_scrl]
    js .out
    cmp ax, FD_ROWS             ; the row is < total, so this bounds it by
    jae .out                    ; what is drawn just as [fdlg_shown] did
    mov cx, FD_ROWH
    mul cx
    add ax, FD_ROW0
    mov bx, ax                  ; y1 = the row band's top
    mov dx, ax
    add dx, FD_ROWH-1
    mov ax, FD_LX1+1
    mov cx, FD_SBX-1
    call fdlg_off
    call KERNEL_SEG:cw_gfx_xor_fill
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_sbset - fill os88ui_ksb with this dialog's bar (internal)
; in:  nothing; the dialog is up
; out: BX = the block; every other register preserved
;
; ONE setter for all three users - the painter, the hit-tester and the thumb
; move - so the four numbers that say where the bar is are written once.
; ABSOLUTE, because the shared element is (a package's bar is not inside a
; kernel window and has no content origin to be relative to).
; -----------------------------------------------------------------------------
fdlg_sbset:
    push ax
    mov ax, [fdlg_cx]
    add ax, FD_SBX
    mov [os88ui_ksb+0], ax
    mov ax, [fdlg_cx]
    add ax, FD_LX2
    mov [os88ui_ksb+4], ax
    mov ax, [fdlg_cy]
    add ax, FD_LY1
    mov [os88ui_ksb+2], ax
    mov ax, [fdlg_cy]
    add ax, FD_LY2
    mov [os88ui_ksb+6], ax
    call fdlg_rows
    mov [os88ui_ksb+8], ax      ; total
    mov word [os88ui_ksb+10], FD_ROWS
    mov ax, [fdlg_scrl]
    mov [os88ui_ksb+12], ax
    mov bx, os88ui_ksb
%ifdef OS88UI_SBDRAG
    call os88ui_sbfix           ; ...and while a thumb drag is live ON THIS
                                ; BAR, word 6 is the HAND's pos rather than
                                ; the view's (SPEC.md 13.10.5.6)
%endif
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_draw_sbar - the scroll bar, through the SHARED element (SPEC.md 13.10)
; in:  the layout cache; gfx lock held
; out: nothing (all registers preserved)
;
; It was 136 bytes of frame, two rules, two hand-plotted arrows, a grey track
; and a thumb - files.inc's 139 bytes of the same thing one file along, and
; the two of them a PIXEL apart in two places. The element is that picture
; once; what this routine is now is where the bar IS.
; -----------------------------------------------------------------------------
fdlg_draw_sbar:
    push bx
    call fdlg_sbset
    call os88ui_sbar
    pop bx
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
    call KERNEL_SEG:cw_wm_content ; AX = x1, DX = y1
    mov cx, [bx+W_X]
    add cx, [bx+W_W]
    sub cx, 2                   ; CX = x2
    push dx
    mov dx, [bx+W_Y]
    add dx, [bx+W_H]
    sub dx, 2                   ; DX = y2
    pop bx                      ; BX = y1
    mov byte [gfx_color], CWHITE
    call KERNEL_SEG:cw_gfx_fill
    call fdlg_paint_x           ; SI is still the window
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
    cmp byte [fdlg_nmode], 0
    jne .mkfolder               ; the box is naming a FOLDER, not a file
    mov ax, [fdlg_sel]
    cmp ax, 0xFFFF
    je .nosel
    call fdlg_stage
    cmp word [fdlg_type], 2
    jb .save                    ; a FILE: commit it, in either mode
    call fdlg_dive              ; a FOLDER: navigate into it, in either mode.
    jmp short .stay             ; Selecting one and pressing Enter in Save mode
.nosel:                         ; used to save a file under whatever name the
                                ; box still held
    ; Open mode with no ROW chosen is not the same as nothing to open. The
    ; box is not editable in Open mode, so the only name it can be holding is
    ; the one the CALLER passed in - and it is drawn, in a box labelled
    ; 'Open:', which is a promise. This used to refuse it: the dialog showed a
    ; file name, and the Open button and Enter both just beeped, while a
    ; double-click on a row worked. The empty-name gate below is what actually
    ; distinguishes "nothing to act on", and it serves both modes.
.save:
    cmp byte [fdlg_nlen], 0
    je .refuse                  ; an empty name is not an answer
    pop ax
    call fdlg_commit
    stc
    ret
.mkfolder:
    call fdlg_newfolder         ; Enter finishes naming it, the same as
    jmp short .stay             ; clicking the button again does
.refuse:
    call KERNEL_SEG:cw_snd_beep
.stay:
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; fdlg_newfolder - the New Folder button, both halves of it
; in:  SI = window ptr; lock held
; out: nothing (all registers preserved)
;
; **The name box holds the FILE name**, which is the whole point of a Save As
; dialog - so using it as the folder name, which the first version of this
; did, created a folder called PICTURE.GIF and then saved the picture inside
; it. The button is therefore a two-step: the first click parks the file name
; and turns the box into a folder-name field (the label changes to say so),
; the second click - or Enter - creates the folder, navigates into it, and
; puts the file name back so the user can carry on saving. Escape backs out
; the same way.
;
; This is the same shape the Disk window's New Folder already has (SPEC.md
; 22: `n` arms a status-line prompt), which is why it needs no new input
; path: the box is already a line editor with the FAT character set enforced
; per keystroke.
; -----------------------------------------------------------------------------
fdlg_newfolder:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [fdlg_nmode], 0
    jne .create

    ; --- first click: park the file name and empty the box ------------------
    mov si, fdlg_name
    mov di, fdlg_nsave
    call fdlg_ncopy
    mov al, [fdlg_nlen]
    mov [fdlg_nslen], al
    mov byte [fdlg_nlen], 0
    mov byte [fdlg_ncur], 0
    mov byte [fdlg_name], 0
    mov byte [fdlg_nmode], 1
    jmp short .out

    ; --- second click: make it, go into it, and restore the file name -------
.create:
    cmp byte [fdlg_nlen], 0
    je .beep                    ; an empty name is not a folder name
    mov si, fdlg_name
    call dskw_mkdir_x           ; CF=1 with AX = FERR_*; on success it has
    jc .beep                    ; already remounted (SPEC.md 18.5), so the
    mov si, fdlg_name           ; listing below is the fresh one
    call fdlg_findname          ; AX = its first cluster, CF=1 = not found
    jc .restore
    mov dl, [disk_drive]
    call fdlg_go
.restore:
    call fdlg_unfold
    jmp short .out
.beep:
    call KERNEL_SEG:cw_snd_beep ; refused (a bad name, a full disk, a
.out:                           ; write-protected volume) - the same answer
    pop di                      ; every other refusal in this dialog gives.
    pop si                      ; SI first: the folder work above pointed it
    call fdlg_repaint           ; at fdlg_name, and fdlg_repaint wants the
    pop dx                      ; WINDOW - through the name it silently draws
    pop cx                      ; nothing and the dialog shows a stale screen
    pop bx                      ; while the volume really has moved. The
    pop ax                      ; repaint is here and not at the call sites
    ret                         ; because both halves of the button change
                                ; what the box says, and one of the two ways
                                ; in (Enter, through fdlg_act) has none

; -----------------------------------------------------------------------------
; fdlg_unfold - leave folder-naming mode, putting the file name back
; out: nothing (all registers preserved). A no-op when the box is already a
;      file name, so every caller can call it unconditionally.
; -----------------------------------------------------------------------------
fdlg_unfold:
    push ax
    push si
    push di
    cmp byte [fdlg_nmode], 0
    je .out
    mov si, fdlg_nsave
    mov di, fdlg_name
    call fdlg_ncopy
    mov al, [fdlg_nslen]
    mov [fdlg_nlen], al
    mov [fdlg_ncur], al
    mov byte [fdlg_nmode], 0
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_ncopy - copy a NUL name between the box and its parking space
; in:  SI = source, DI = destination (both 16 bytes, in our own segment)
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_ncopy:
    push ax
    push cx
    push si
    push di
    mov cx, 16
.b:
    mov al, [si]
    mov [di], al
    inc si
    inc di
    loop .b
    pop di
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_findname - the first cluster of the directory named SI, in this listing
; in:  SI = a NUL 8.3 name; the volume already listed
; out: CF=0 with AX = its first cluster; CF=1 = no such folder
; clobbers: AX (the output), CF
; -----------------------------------------------------------------------------
fdlg_findname:
    push bx
    push cx
    push dx
    push si
    push di
    mov cx, [disk_nfiles]
    jcxz .no
    xor bx, bx                  ; BX = entry index
.scan:
    push cx
    push bx
    mov ax, bx
    call dsk_get_dir_x ; SI -> the staged entry: name, then the type
    cmp word [si+16], 2         ; word at +16 and the first cluster at +18
    jne .next                   ; (SPEC.md 19) - only a folder can match
    mov di, si
    mov si, fdlg_name
    call fdlg_streq             ; CF=0 = same name
    jc .next
    pop bx
    pop cx
    mov ax, bx
    call dsk_get_dir_x
    mov ax, [si+18]
    clc
    jmp short .out
.next:
    pop bx
    pop cx
    inc bx
    loop .scan
.no:
    stc
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fdlg_streq - do the NUL strings at DS:SI and DS:DI match? (internal)
; out: CF=0 = equal; preserves all registers
; -----------------------------------------------------------------------------
fdlg_streq:
    push ax
    push si
    push di
.c:
    mov al, [si]
    cmp al, [di]
    jne .no
    or al, al
    jz .yes
    inc si
    inc di
    jmp short .c
.no:
    stc
    jmp short .out
.yes:
    clc
.out:
    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_dive - enter the staged row's directory and repaint (internal)
; in:  fdlg_type staged and >= 2, SI = window ptr; lock held
; out: nothing (all registers preserved)
;
; '..' needs no special case: the mount put the PARENT'S first cluster in
; that entry (SPEC.md 19.5), so every folder here is one cluster read out of
; one staged entry. This used to test fdlg_row[0] for a dot and call
; dsk_dotdot, which walked the directory off the disk a second time.
; -----------------------------------------------------------------------------
fdlg_dive:
    push ax
    push cx
    push dx
    push si
    mov ax, [fdlg_sel]
    call dsk_get_dir_x ; SI -> the staged entry
    mov ax, [si+18]             ; its first cluster (SPEC.md 19)
%ifdef KERN_BIG
    push ax                     ; ...and the NAME, while the staged entry is
    push bx                     ; still there: fdlg_go below remounts, and a
    mov bx, fdlg_path           ; mount rebuilds dsk_ent - the same trap
    cmp word [si+16], 3         ; fm_open_sel banks its own two values against
    je .updir                   ; 3 = the parent link (19.5): the way UP
    call pth_push               ; SI -> the entry's 16-byte name field
    jmp short .pdone
.updir:
    call pth_pop                ; ...which is where the name comes BACK from
.pdone:
    pop bx
    pop ax
%endif
    pop si
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
fdlg_onkey_x:
    push ax
    push bx
    push cx
    push dx
    push di

    push ax                     ; the content origin the partial redraws
    mov bx, si                  ; below need. This proc used to get it for
    call KERNEL_SEG:cw_wm_content ; free from fdlg_paint, which it no longer
    mov [fdlg_cx], ax           ; always reaches - and AX is the keystroke,
    mov [fdlg_cy], dx           ; which the ladder below still wants
    pop ax

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
    or al, al
    jnz .typing                 ; an EXTENDED key has AL = 0. The numeric
    cmp ah, 0x4B                ; keypad sends '4' '6' '7' '1' '.' with these
    je .kleft                   ; very scan codes, so without this gate
    cmp ah, 0x4D                ; NumLock would turn typing a digit into
    je .kright                  ; moving the caret
    cmp ah, 0x47
    je .khome
    cmp ah, 0x4F
    je .kend
    cmp ah, 0x53
    je .kdel
.typing:
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
    cmp byte [fdlg_ncur], 0
    je .out                     ; a short name may not start with the dot...
    call fdlg_hasdot
    jc .out                     ; ...and may not carry a second one
    jmp short .store
.plain:
    call dskw_char_x            ; upper-cases, CF = 1 = not in the FAT set
    jc .out
.store:
    call fdlg_nins              ; at the caret, which moves past it
    jmp .rname

.bksp:
    cmp byte [fdlg_ncur], 0
    je .out                     ; nothing to the left of the caret
    dec byte [fdlg_ncur]
    call fdlg_ndel
    jmp .rname

.kdel:
    call fdlg_ndel              ; forward delete: the caret stays put
    jmp .rname
.kleft:
    cmp byte [fdlg_ncur], 0
    je .out
    dec byte [fdlg_ncur]
    jmp short .rname
.kright:
    mov al, [fdlg_ncur]
    cmp al, [fdlg_nlen]
    jae .out
    inc byte [fdlg_ncur]
    jmp short .rname
.khome:
    mov byte [fdlg_ncur], 0
    jmp short .rname
.kend:
    mov al, [fdlg_nlen]
    mov [fdlg_ncur], al
    jmp short .rname

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
    jmp short .rboth

.enter:
    call fdlg_act               ; CF = 1: the window is gone, do not repaint
    jmp short .out
.cancel:
    cmp byte [fdlg_nmode], 0    ; Escape while naming a folder backs OUT of
    je .close                   ; that, not out of the dialog: the user asked
    call fdlg_unfold            ; for a folder, changed their mind, and still
    jmp short .redraw           ; wants to save. WHOLE: the box's label goes
                                ; back from 'Folder:' to 'Save as:'
.close:
    call fdlg_close
    jmp short .out
.rname:                         ; the box changed and nothing else did
    call fdlg_draw_name
    jmp short .out
.rboth:                         ; ...the selection moved, so the list did too
    call fdlg_draw_both
    jmp short .out
.redraw:
    call fdlg_repaint
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; fdlg_nins - insert AL at the caret, which then sits after it (internal)
; in:  AL = the character, already through dskw_char; [fdlg_nlen] < FD_NAMEMAX
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fdlg_nins:
    push ax
    push bx
    push cx
    push dx
    mov dl, al
    mov bl, [fdlg_nlen]
    xor bh, bh
    mov cl, bl
    sub cl, [fdlg_ncur]
    xor ch, ch                  ; CX = the characters right of the caret
    inc bx                      ; ...and the terminator moves with them
.mv:
    mov al, [bx+fdlg_name-1]
    mov [bx+fdlg_name], al
    dec bx
    jcxz .place
    dec cx
    jmp short .mv
.place:
    mov bl, [fdlg_ncur]
    xor bh, bh
    mov [bx+fdlg_name], dl
    inc byte [fdlg_nlen]
    inc byte [fdlg_ncur]
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_ndel - delete the character the caret sits in front of (internal)
; out: nothing (all registers preserved). A caret at the end deletes nothing.
; -----------------------------------------------------------------------------
fdlg_ndel:
    push ax
    push bx
    mov al, [fdlg_ncur]
    cmp al, [fdlg_nlen]
    jae .out
    mov bl, al
    xor bh, bh
.mv:
    mov al, [bx+fdlg_name+1]    ; pull the tail down over it, terminator
    mov [bx+fdlg_name], al      ; included
    or al, al
    jz .done
    inc bx
    jmp short .mv
.done:
    dec byte [fdlg_nlen]
.out:
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
    cmp byte [fdlg_nmode], 0
    jne .no                     ; the box is a folder name being typed: a
    mov ax, [fdlg_sel]          ; selection must not overwrite it
    cmp ax, 0xFFFF
    je .no
    call fdlg_stage
    cmp word [fdlg_type], 2
    jae .no                     ; a folder is not a file name
    mov si, fdlg_row
    call fdlg_setname           ; CF = 1 = the box is not what it was
    jmp short .out
.no:
    clc                         ; nothing to pick is nothing to redraw
.out:
    pop si
    pop ax
    ret

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; fdlg_onup - W_ONMOUSEUP (SPEC.md 13.7): the button fires HERE
; in:  CX = x, DX = y (SCREEN, possibly outside the window), SI = window ptr;
;      gfx lock held
; out: nothing (all registers preserved)
;
; The pressed button is put back up FIRST and unconditionally, before anything
; acts - fdlg_act and fdlg_close both DESTROY this window, so a later restore
; would draw into a dead one (SPEC.md 13.8.2's rule, with teeth here).
; -----------------------------------------------------------------------------
fdlg_onup_x:
    push ax
    push bx
    push cx
    push dx
    push di
    call fdlg_pt                ; CX = content y, DX = content x
%ifdef OS88UI_SBDRAG
    call os88ui_sbdragging      ; SPEC.md 13.10.5: the release COMMITS, and
    jc .btn                     ; unconditionally - a position the rate refused
    call fdlg_sbset             ; mid-drag is never lost, and a release that
    call os88ui_sbdrop          ; landed anywhere still ends the gesture
    jc .btn                     ; CF = 1 = it was the Disk window's bar
    cmp ax, [fdlg_scrl]
    je .out                     ; ...but a release on the row the press began
    mov [fdlg_scrl], ax         ; on scrolled nothing, and this list has no
    call fdlg_draw_list         ; cheaper tier than a fill and FD_ROWS rows -
    jmp short .out              ; fm_scroll_by's own `jz .same` (SPEC.md 22.11)
.btn:
%endif
    call os88ui_fire            ; AX = what the press armed, and it is CLEARED
    or ax, ax
    jz .out
    mov di, ax
    xor ax, ax
    call fdlg_setdown           ; ...upright again, whatever happens next
    call fdlg_bhit              ; AX = the button under the RELEASE
    cmp ax, di
    jne .out                    ; a different button, or none: CANCELLED
    call fdlg_bact
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                          ; KERN_BIG

; -----------------------------------------------------------------------------
; fdlg_bact - do what button AX does
; in:  AX = fdlg_bhit's index (1..4); gfx lock held
; out: nothing; every register clobbered (fdlg_act can destroy the window)
;
; ONE ACTION LADDER FOR BOTH BUILDS, which is what makes the split honest:
; kern_big calls it from the RELEASE (SPEC.md 13.8.3) and kern_small from the
; press, and the difference between the two kernels is then WHICH EDGE calls
; this - not two copies of what the four buttons do.
; -----------------------------------------------------------------------------
fdlg_bact:
    dec ax
    jz .b_def
    dec ax
    jz .b_cancel
    dec ax
    jz .b_drive
    jmp fdlg_newfolder          ; repaints itself, both halves
.b_def:
    jmp fdlg_act                ; CF = 1 = the window is gone
.b_cancel:
    jmp fdlg_close
.b_drive:
    call fdlg_nextvol           ; the next LIVE volume, at its root (SPEC.md
    xor ax, ax                  ; 38.11); disk_mount publishes [disk_drive]
    call fdlg_go
    jmp fdlg_repaint

%ifdef KERN_BIG
; -----------------------------------------------------------------------------
; fdlg_ondrag - W_ONDRAG (SPEC.md 13.8.2): the pointer moved, press still down
; in:  CX = x, DX = y (SCREEN), SI = window ptr; gfx lock held
; out: nothing (all registers preserved)
;
; The same question fdlg_onup asks at the release, one pass early, so what is
; drawn down is exactly what would fire. os88ui_armed and not os88ui_fire:
; the arm has to survive this and be there for the release.
; -----------------------------------------------------------------------------
fdlg_ondrag_x:
    push ax
    push bx
    push cx
    push dx
    push di
%ifdef OS88UI_SBDRAG
    call os88ui_sbdragging      ; SPEC.md 13.10.5: a live thumb drag owns this
    jc .btn                     ; movement whole
    push dx                     ; the pointer's ABSOLUTE y - fdlg_pt eats DX,
    call fdlg_pt                ; and it is also what refreshes [fdlg_cx]/[cy]
    call fdlg_sbset             ; for the setter
    pop dx
    call os88ui_sbtrack         ; CF = 1: nothing owed yet - the rate, the same
    jc .out                     ; row, or another bar's gesture
    mov [fdlg_scrl], ax
    call fdlg_draw_list         ; no incremental tier here: the list and its
    jmp short .out              ; bar, whole (SPEC.md 13.10.6.2)
.btn:
%endif
    call os88ui_armed
    or ax, ax
    jz .out                     ; the press landed on nothing: nothing to track
    mov di, ax
    call fdlg_pt
    call fdlg_bhit
    cmp ax, di
    je .set
    xor ax, ax                  ; off it: nothing down
.set:
    call fdlg_setdown           ; ...which draws only if that CHANGED
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
%endif                          ; KERN_BIG

; -----------------------------------------------------------------------------
; fdlg_pt - a SCREEN point to a content-relative one, layout cache refreshed
; in:  CX = x, DX = y (screen), SI = window ptr
; out: CX = content y, DX = content x - fdlg_bhit's argument order; AX and BX
;      clobbered
;
; Factored out of fdlg_onclick because three edges now need it and the
; conversion of one into three is exactly where a second copy of this
; arithmetic would have been written.
; -----------------------------------------------------------------------------
fdlg_pt:
    mov bx, si
    push cx                     ; save the click x across wm_content
    mov cx, dx                  ; CX = click y
    call KERNEL_SEG:cw_wm_content ; AX = content left, DX = content top
    mov [fdlg_cx], ax
    mov [fdlg_cy], dx
    sub cx, dx                  ; CX = content-relative y
    pop dx
    sub dx, ax                  ; DX = content-relative x
    ret

; -----------------------------------------------------------------------------
; fdlg_setdown - MOVE the pressed state to button AX (0 = none)
; in:  AX = fdlg_bhit's index, or 0; the layout cache is current; gfx lock held
; out: nothing (all registers preserved)
;
; The ONE writer of [fdlg_down], which is what keeps "which button is drawn
; down" and "which button the variable says is down" from parting - Calculator's
; cal_setdown exactly (SPEC.md 65.8), and for the same reason: [fdlg_down] is
; read by fdlg_paint's button column, so a repaint mid-gesture draws the held
; button held.
;
; It draws NOTHING when the state has not changed, which is what makes the
; tracking edge affordable: a pointer moving INSIDE the button it pressed
; costs one compare.
;
; Both buttons are redrawn with FILL, because the ground under the one going
; up is a pressed button and the ground under the one coming down is an
; upright one. That is os88ui.inc's OS88UI_FILL question answered "yes" on
; both counts, and it is why neither of these is an XOR (SPEC.md 13.8).
; -----------------------------------------------------------------------------
%ifdef KERN_BIG
fdlg_setdown:
    push ax
    cmp ax, [fdlg_down]
    je .out
    push bx
    xchg ax, [fdlg_down]        ; AX = the OLD one, and the variable is already
    or ax, ax                   ; the new one - so a repaint racing this reads
    jz .down                    ; the new state on either side of the draw
    call fdlg_btn1
.down:
    mov ax, [fdlg_down]
    or ax, ax
    jz .done
    call fdlg_btn1
.done:
    pop bx
.out:
    pop ax
    ret
%endif                          ; KERN_BIG

; -----------------------------------------------------------------------------
; fdlg_btn1 - redraw button AX of the column, down or up as [fdlg_down] says
; in:  AX = fdlg_bhit's index (1..4); gfx lock held
; out: nothing (all registers preserved)
;
; The DOWN state is read from [fdlg_down] rather than passed, so this one
; routine serves fdlg_setdown's two edges AND fdlg_paint's full column and
; they cannot disagree (SPEC.md 13.8.2's rule for a package, which is the
; kernel's here too).
; -----------------------------------------------------------------------------
fdlg_btn1:
    push ax
    push bx
    push cx
    push si
    push di
    mov di, ax                  ; DI = which
    xor cx, cx                  ; CL = the extra flags
    cmp ax, [fdlg_down]
    jne .up
    mov cl, OS88UI_DOWN
.up:
    dec di
    jz .b_def                   ; 1: the default button draws itself, greying
    dec di                      ; and all (fdlg_defbtn)
    jz .b_cancel
    dec di
    jz .b_drive
    mov si, fdlg_s_newf         ; 4: New / Folder, two lines
    mov di, fdlg_s_newf2
    mov bx, FD_BY3
    mov ax, cx                  ; AL = the flags (DOWN or nothing)
    call fdlg_btn2
    jmp short .out
.b_cancel:
    mov si, fdlg_s_cancel
    mov bx, FD_BY1
    jmp short .plain
.b_drive:
    mov si, fdlg_s_drive
    mov bx, FD_BY2
.plain:
    mov ax, cx                  ; AL = the flags, AH = 0: never greyed
    or al, OS88UI_FILL          ; the ground is the other state of itself
    call fdlg_btn
    jmp short .out
.b_def:
    call fdlg_defbtn            ; reads [fdlg_down] itself, having its own
.out:                           ; flag word to build
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fdlg_bhit - which button of the right-hand column is this point in?
; in:  DX = x, CX = y, both CONTENT-relative
; out: AX = 1 Open/Save, 2 Cancel, 3 Drive, 4 New Folder; 0 = none.
;      All other registers preserved
;
; ONE DESCRIPTION, read by the press, the tracking edge and the release alike
; (SPEC.md 13.8.3) - which is what a ladder of inline coordinates could not be,
; and is fm_hit's discipline (SPEC.md 22) arriving where os88ui.inc's rect
; pointer could not reach: these four share an x span and differ only in y, so
; the shared rect table would be four copies of the same two columns.
;
; The index is 1-based because 0 means "none", which is os88ui_bfind's own
; sentinel and what os88ui_arm/fire carry.
;
; New Folder is NOT DRAWN in Open mode (SPEC.md 38.3), so it is not here
; either - the drawn control and the clickable control being the same control
; is the whole point of the routine.
; -----------------------------------------------------------------------------
fdlg_bhit:
    push cx
    cmp dx, FD_BX1
    jl .none
    cmp dx, FD_BX2
    jg .none
    sub cx, FD_BY0
    js .none
    mov ax, 1                   ; Open / Save
    cmp cx, FD_BH
    jle .out
    sub cx, FD_BY1 - FD_BY0
    js .none
    inc ax                      ; Cancel
    cmp cx, FD_BH
    jle .out
    sub cx, FD_BY2 - FD_BY1
    js .none
    inc ax                      ; Drive
    cmp cx, FD_BH
    jle .out
    cmp byte [fdlg_mode], 0
    je .none                    ; Open mode: no New Folder drawn, none clickable
    sub cx, FD_BY3 - FD_BY2
    js .none
    inc ax                      ; New Folder, and it is FD_BH2 tall - two lines
    cmp cx, FD_BH2
    jle .out
.none:
    xor ax, ax
.out:
    pop cx
    ret

; -----------------------------------------------------------------------------
; fdlg_onclick - W_ONCLICK (SPEC.md 38.3)
; in:  CX = x, DX = y (absolute), SI = window ptr; gfx lock held
; out: nothing (all registers preserved)
; Test order: buttons, then the scroll bar, then the rows - the SPEC.md 22
; order, and for the same reason: the bar overlaps the row band's right edge
; and has to win there.
; -----------------------------------------------------------------------------
fdlg_onclick_x:
    push ax
    push bx
    push cx
    push dx
    push di

    call fdlg_pt                ; CX = content y, DX = content x
%ifdef OS88UI_SBDRAG
    push cx                     ; A PRESS CANNOT ARRIVE DURING A LIVE DRAG, so
    push dx                     ; one that does means the release never came
    call os88ui_sbdragging      ; (SPEC.md 13.10.5.7) - fm_onclick's net, and
    jc .nostale                 ; the dialog needs its own because it is MODAL:
    call fdlg_sbset             ; while it is up fdlg_grab swallows the clicks
    call os88ui_sbdrop          ; that would otherwise reach the other one
.nostale:
    pop dx
    pop cx
%endif

    ; --- the button column ---------------------------------------------
    ; kern_big: the press ARMS and draws down and acts on NOTHING (SPEC.md
    ; 13.8.3) - a mis-aimed press used to fire Cancel. kern_small keeps the
    ; old behaviour, which is all it can afford, and the two share both the
    ; hit test AND the action, so what differs between the kernels is only
    ; WHICH EDGE acts.
    call fdlg_bhit
    or ax, ax
    jz .sbar
%ifdef KERN_BIG
    call os88ui_arm
    call fdlg_setdown
%else
    call fdlg_bact
%endif
    jmp .out

    ; --- the name box: put the caret where the user pointed ---
    ; This is the first thing every click that missed the button column sees,
    ; which is why the scroll bar's own test is a label further down: the
    ; button column jumps HERE when it misses, so anything reached only by
    ; falling out of it would never see a click on the left of the dialog.
.sbar:
    cmp byte [fdlg_mode], 0
    je .sbar2                   ; nothing to type there in Open mode
    cmp cx, FD_NBY1
    jl .sbar2
    cmp cx, FD_NBY2
    jg .sbar2
    cmp dx, FD_NBX1
    jl .sbar2
    cmp dx, FD_NBX2
    jg .sbar2
    mov ax, dx                  ; column = (x - the text origin + half a
    sub ax, FD_NTX - 4          ; cell) / 8, so a click in the left half of a
    jns .nb_ok                  ; character puts the caret before it
    xor ax, ax
.nb_ok:
    mov cl, 3
    shr ax, cl
    cmp al, [fdlg_nlen]
    jbe .nb_set
    mov al, [fdlg_nlen]         ; past the text: the caret goes after it
.nb_set:
    mov [fdlg_ncur], al
    jmp .rname

    ; --- the scroll bar ---
.sbar2:
    ; SPEC.md 13.10: the shared element owns where the parts ARE and this
    ; owns what they MEAN - which is why it reports the thumb as a part of its
    ; own. files.inc pages down on it and this does nothing, and both are
    ; right about their own window.
    cmp dx, FD_SBX
    jl .rows
    push cx                     ; it takes the point ABSOLUTE (a package's bar
    push dx                     ; has no content origin), and this ladder is
    mov ax, cx                  ; content-relative with x in DX and y in CX
    mov cx, dx
    add cx, [fdlg_cx]
    mov dx, ax
    add dx, [fdlg_cy]
    call fdlg_sbset
    call os88ui_sbhit
%ifdef OS88UI_SBDRAG
    cmp al, OS88UI_SBTHUMB      ; SPEC.md 13.10.5: the thumb is DRAGGED here
    jne .nograb2                ; now, where it did nothing at all. BX is
    push ax                     ; still the block and DX still the press,
    mov al, FD_SBRATE           ; absolute - and AL has to survive, because a
    call os88ui_sbgrab          ; unchanged and fall through to .out, which is
    pop ax                      ; what a thumb press has always done here
.nograb2:
%endif
    pop dx
    pop cx
    cmp al, OS88UI_SBUP
    je .s_up
    cmp al, OS88UI_SBDOWN
    je .s_dn
    cmp al, OS88UI_SBPGUP
    je .s_pgup
    cmp al, OS88UI_SBPGDN
    je .s_pgdn
    jmp .out                    ; the thumb itself, or off the bar entirely
.s_up:
    dec word [fdlg_scrl]
    jmp .rlist
.s_dn:
    inc word [fdlg_scrl]
    jmp .rlist
.s_pgup:
    sub word [fdlg_scrl], FD_ROWS
    jmp .rlist
.s_pgdn:
    add word [fdlg_scrl], FD_ROWS
    jmp .rlist

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
    jae .again
    call fdlg_stage
    cmp word [fdlg_type], 2
    jae .dive
    cmp byte [fdlg_mode], 0     ; a double-clicked FILE: in Open mode that
    jne .again                  ; is the answer; in Save mode it is only a
    call fdlg_commit            ; name for the box, which the first click of
    jmp short .out              ; the pair already put there
.dive:
    call fdlg_dive              ; repaints itself
    jmp short .out

    ; The row is ALREADY selected, so no band moves and the list does not
    ; change one pixel (SPEC.md 38.3). The box still follows the selection -
    ; that is what clicking a row MEANS in Save mode - so fdlg_pick still
    ; runs, and its CF is the whole question: it comes back set only when
    ; something was typed into the box since the last click, which is the one
    ; way a second click on the same row can change anything.
.again:
    mov bx, [ui_click_t]
    mov [fdlg_clkt], bx
    call fdlg_pick
    jc .rname
    jmp short .out

    ; A DIFFERENT row: the old band comes off, the new one goes on, and the
    ; rows, their sizes and the whole scroll bar stay where they are. The
    ; fallback is for fdlg_setsel scrolling the selection into view - which a
    ; click never does, the hit test being bounded by what is drawn, but the
    ; keyboard shares that routine and this is the cheaper thing to prove.
.select:
    push ax
    mov ax, [fdlg_sel]
    call fdlg_sel_bar           ; off (a no-op at 0FFFFh)
    pop ax
    mov bx, [fdlg_scrl]
    push bx
    call fdlg_setsel
    pop bx
    mov ax, [ui_click_t]
    mov [fdlg_clkt], ax
    cmp bx, [fdlg_scrl]
    jne .selscrl
    mov ax, [fdlg_sel]
    call fdlg_sel_bar           ; ...and on
    call fdlg_pick              ; the box follows a file, not a folder
    jc .rname
    jmp short .out
.selscrl:
    call fdlg_pick
    call fdlg_draw_both         ; it scrolled: the whole list moved
    jmp short .out
.rlist:                         ; the listing scrolled; the box did not move
    call fdlg_draw_list
    jmp short .out
.rname:                         ; only the caret in the box moved
    call fdlg_draw_name
    jmp short .out
.redraw:
    call fdlg_repaint           ; SI is still the window ptr
.out:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
section .bss

                                ; (fdlg_win, the gate, is NOT here - it is a
                                ; `dw 0` up in .text; see the note there)
fdlg_down:  resw 1              ; SPEC.md 13.8.3: which button of the column is
                                ; drawn DOWN, as fdlg_bhit's 1-based index
                                ; (0 = none). fdlg_setdown is its one writer
                                ; and fdlg_btn1 its one reader, so the full
                                ; paint and the two edges cannot disagree.
                                ; A gesture cannot outlive the dialog - both
                                ; fdlg_act and fdlg_close destroy the window -
                                ; but fdlg_open clears it anyway, because the
                                ; NEXT dialog is a fresh gesture and a stale 1
                                ; would draw Open pressed the moment it opened
fdlg_mode:  resb 1              ; 0 = Open, 1 = Save
fdlg_rqwin: resw 1              ; the requester's window...
fdlg_hdrv:  resb 1              ; fdlg_home_go's fallback: the volume the
fdlg_hcwd:  resw 1              ; MACHINE is on, banked before this instance's
                                ; own folder is tried, because a chdir that
                                ; FAILS moves the globals it would otherwise
                                ; be re-read from
fdlg_idrv:  resb 1              ; ...and the INSTANCE's drive, banked across
                                ; the MEDIA lookup, which needs it for the
                                ; chdir to the root and again for the chdir
                                ; into the folder that root named
%ifdef KERN_BIG
fdlg_path:  resb PTH_SIZE       ; the folders this dialog was walked down
                                ; through (docs/TITLE-PLAN.md 5), the PTH_*
                                ; block files.inc defines. ONE of them, not
                                ; one per window: the dialog is MODAL, so it
                                ; can only ever be in one place at a time -
                                ; the same fact that lets it read the global
                                ; mount snapshot instead of a view cache
                                ; (SPEC.md 38.2). .bss is safe because
                                ; fdlg_home_go clears it on every open, which
                                ; is what the reused KD_POOL block needs too
%endif
fdlg_rqcb:  resw 1              ; ...its near completion proc...
fdlg_rqrec: resw 1              ; ...its instance record...
fdlg_rqsp:  resw 1              ; ...and that record's I_SPTR: the staleness
                                ; triple of SPEC.md 38.6
fdlg_sel:   resw 1              ; selected DISPLAY row, 0FFFFh = none
fdlg_scrl:  resw 1              ; first visible row
fdlg_clkt:  resw 1              ; birth tick of the last click on the
                                ; selected row (the double-click window)
fdlg_name:  resb 16             ; the answer: the caller's default in, the
                                ; chosen name out, the edit buffer between
fdlg_nlen:  resb 1              ; its length, 0..12
fdlg_nmode: resb 1              ; 0 = the box holds the FILE name, 1 = it is
                                ; naming a new folder (SPEC.md 38.3)
fdlg_nsave: resb 16             ; ...and the file name parked while it does
fdlg_nslen: resb 1
fdlg_ncur:  resb 1              ; ...and the CARET, 0..[fdlg_nlen]: the
                                ; character it sits in front of. Typing,
                                ; backspace and delete all act here, which is
                                ; what makes the box an editor rather than an
                                ; append-only field (SPEC.md 38.5)
fdlg_cx:    resw 1              ; content origin, cached by paint and click
fdlg_cy:    resw 1
fdlg_shown: resw 1              ; rows the painter actually drew: scratch,
                                ; valid inside one draw and nowhere else -
                                ; which is why fdlg_sel_bar, reachable from a
                                ; click, asks fdlg_rows instead
fdlg_row:   resb 18             ; one staged row's name (16 + NUL, padded)
fdlg_type:  resw 1              ; and its SPEC.md 19 type...
fdlg_size:  resw 1              ; ...and size dword, lo...
fdlg_sizeh: resw 1              ; ...and hi (SPEC.md 19)
fdlg_num:   resb 11             ; the size column's digits (fm_ultoa: 10+NUL)
fdlg_bdef:  resb 1              ; fdlg_btn: is this the default button?
fdlg_actl:  resb 1              ; fdlg_actok's answer as last DRAWN, so a
                                ; keystroke redraws the default button on the
                                ; live/dead edge and not on every caret move

section .text

section .cold                   ; far entries (SPEC.md 2.6): the resident
                                ; thunks in kernel.asm far-call these, and
                                ; these near-call the bodies above - so every
                                ; routine here keeps its ordinary near `ret`
%ifdef KERN_BIG
fdf_fdlg_onup:          call fdlg_onup_x
                    retf
fdf_fdlg_ondrag:        call fdlg_ondrag_x
                    retf
%endif
fdf_fdlg_open:          call fdlg_open_x
                    retf
fdf_fdlg_paint:         call fdlg_paint_x
                    retf

section .text
