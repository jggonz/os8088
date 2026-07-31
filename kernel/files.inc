; =============================================================================
; os8088 - files.inc
;
; The Disk window - a minimal file manager for the FAT data floppy
; (SPEC.md 22). A singleton app kind (KIND_FILES, cap 1 - the mount state
; below is module-global): no boot-time window; app_launch creates it on
; demand (fm_kinit is the KD_INIT). CMD_FILES dispatch calls files_open and
; desk_click calls files_open_drive - both ALWAYS remount (that is the
; disk-swap story: a swapped disk must never show stale contents) and end
; in app_launch, which creates the window or fronts + un-minimizes the
; existing instance.
;
; The window is RESIZABLE (KD_WFLAG = WF_SIZABLE, the one built-in that is)
; and the directory SCROLLS, in either of two views: a list (16px rows:
; icon + name + size) or an icon grid (78x40 cells). Nothing may bake in
; the template's 320x200: fm_layout derives every coordinate both the
; painter and the hit-tester use from the live window record, in one
; place, so they can never disagree (SPEC.md 22 "Live layout").
;
; Opening an entry - a double-click, Enter, or File > Open - goes through
; ONE routine, fm_open_sel, which branches on the SPEC.md 19 type word: a
; package posts index+1 to [ld_pending] (ui.inc runs loader_run after the
; lock drops and reports through [ld_status]), a FOLDER calls dsk_chdir and
; the whole window follows it - listing, title, selection, scroll.
;
; The window also owns the menu bar. fm_menus (below) is Locator's SECOND
; SPEC.md 12.2 menu set: same AM_NAME, so the bar still reads 'Locator',
; but a real AM_ONCMD - fm_oncmd - so File/Folder/View/Special are this
; window's commands rather than the desktop's. fm_oncmd runs under the gfx
; lock exactly like W_ONCLICK, which is why three of its commands are
; POSTED (inst_launch_post / ui_post_cmd) instead of called: see the jump
; table's per-entry comments, and SPEC.md 22 for the binding list.
;
; Naming a new folder happens in the status line rather than in a dialog
; (there is no dialog kind and no focus concept): [fm_edit] turns W_ONKEY
; into a line editor that SWALLOWS EVERY KEY - binding, because otherwise
; the first letter of "BAK" would switch to drive B: - and filters each
; character through diskw.inc's own dskw_char, so the line shows exactly
; the bytes dskw_mkdir will store.
;
; The paint/onkey/onclick procs and fm_oncmd all run with the gfx lock
; already held by the caller (SPEC.md 11/12.2) and never lock, block or
; spawn; files_open / files_open_drive / files_refresh take the lock
; themselves.
; =============================================================================

; --- layout constants (content-relative, SPEC.md 22) -------------------------
FM_ROW_H    equ 16              ; list row height, px
FM_ROW_Y0   equ 22              ; row area top (both views)
FM_DBLCLK   equ 9               ; double-click window, ticks
FM_SB_W     equ 14              ; scroll bar width
FMI_CELL_W  equ 78              ; icon-view cell
FMI_CELL_H  equ 40

; Everything else - buttons, status line, rows that fit, columns - comes
; from fm_layout at run time (the window resizes).

; -----------------------------------------------------------------------------
; files_init - reset module state (no window is created at boot, SPEC.md 22)
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; .bss is not zeroed by the loader: every state variable is set here.
; -----------------------------------------------------------------------------
files_init:
    mov word [fm_sel], 0xFFFF   ; no selection
    mov word [fm_clkt], 0
    mov byte [fm_mountok], 0
    mov byte [fm_view], 0       ; list view
    mov word [fm_scroll], 0
    mov byte [fm_ferr], 0       ; no file-operation error to report
    mov byte [fm_edit], 0       ; not naming anything
    mov byte [fm_elen], 0
    mov byte [fm_ebuf], 0
    mov byte [fm_full], 0
    mov word [fm_titlep], 0     ; no window, so nothing to retitle
    ret

; -----------------------------------------------------------------------------
; fm_kinit - KD_INIT for the Disk kind (SPEC.md 29.3): fresh instance state
; in:  BX = window ptr, DI = state ptr (0 - stateless), SI = instance record
; out: nothing (all registers preserved)
;
; The title is aimed at the INSTANCE RECORD's I_NAME rather than at the
; template's literal, which is what makes the window's caption, its dock
; tile (SPEC.md 30) and its Task Manager row (SPEC.md 28) all follow the
; folder from one 16-byte write. app_launch has already copied KD_NAME
; ('Disk') in there, so the initial caption is unchanged.
; -----------------------------------------------------------------------------
fm_kinit:
    push ax
    mov word [fm_sel], 0xFFFF   ; a fresh window starts with no selection
    mov word [fm_clkt], 0
    mov byte [fm_view], 0       ; list view, scrolled to the top
    mov word [fm_scroll], 0
    mov byte [fm_ferr], 0
    mov byte [fm_edit], 0       ; a reopened window never resumes a
    mov byte [fm_elen], 0       ; half-typed name
    mov byte [fm_ebuf], 0
    mov byte [fm_full], 0
    mov ax, si
    add ax, I_NAME              ; the record's 16-byte name field, which
    mov [fm_titlep], ax         ; inst_set_name zero-filled and NUL-capped
    mov [bx + W_TITLE], ax
    mov word [bx + W_MENUS], fm_menus
                                ; the Disk browser IS Locator's window
                                ; (SPEC.md 12.3): fronting it keeps Locator
                                ; in the bar rather than swapping it for an
                                ; app named 'Disk' - it just swaps Locator's
                                ; desktop menus for its file-manager ones
    pop ax
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
fm_tpl:     dw 110, 80, 320, 200, fm_ttl, fm_paint, fm_onkey, fm_onclick
fm_ttl:     db 'Disk', 0

fm_s_drive:  db 'Drive ', 0     ; header: "Drive " letter ":  " N " files"
fm_s_colsp:  db ':  ', 0
fm_s_files:  db ' files', 0
fm_s_nodisk: db 'No os8088 disk (', 0 ; + letter + ':)' - 19 chars, short
                                ; enough to clear the buttons (SPEC.md 22)
fm_s_refresh: db 'Refresh', 0   ; button label, 7 chars = 56 px
fm_s_icons:   db 'Icons', 0     ; view-toggle labels: the view a click
fm_s_list:    db 'List', 0      ; switches TO (SPEC.md 22)

fm_s_loading: db 'Loading...', 0
fm_s_empty:   db 0              ; status line strings, indexed by [ld_status]
fm_s_dskerr:  db 'Disk error', 0
fm_s_badpkg:  db 'Bad package', 0
fm_s_toobig:  db 'Too large', 0
fm_s_ldfail:  db 'Load failed', 0
fm_s_nomem:   db 'Out of memory', 0
fm_stattab:   dw fm_s_empty, fm_s_dskerr, fm_s_badpkg, fm_s_toobig, fm_s_ldfail
              dw fm_s_nomem

; --- FERR_* -> status line (SPEC.md 22) --------------------------------------
; Every dskw_* failure has to say something. Without this table a refused
; mkdir - name taken, disk full, write-protected media - looks exactly like
; a successful one, and for operations that touch the user's disk that is
; the worst possible outcome. Indexed by the FERR_* code; slot 0 is never
; drawn (fm_ferr = 0 means "nothing to report") and only exists so the
; index is the code. 'Disk error' is shared with the loader's table above.
fm_s_enodisk: db 'No disk', 0
fm_s_ename:   db 'Bad name', 0
fm_s_enoent:  db 'No such file', 0
fm_s_eexist:  db 'Name exists', 0
fm_s_efull:   db 'Disk full', 0
fm_s_edfull:  db 'Folder full', 0
fm_s_eprot:   db 'Protected', 0
fm_s_ewprot:  db 'Write protected', 0
fm_errtab:    dw fm_s_empty,  fm_s_enodisk, fm_s_dskerr, fm_s_ename
              dw fm_s_enoent, fm_s_eexist,  fm_s_efull,  fm_s_edfull
              dw fm_s_eprot,  fm_s_ewprot,  fm_s_toobig
FM_NERR       equ 11            ; FERR_OK .. FERR_BIG

fm_s_newfld:  db 'New folder: ', 0  ; the edit line's prompt (SPEC.md 22)
fm_s_root:    db 'Disk', 0      ; window title at the root...
fm_s_folder:  db 'Folder', 0    ; ...and in a folder we arrived at from
                                ; below, whose name would cost a second
                                ; mount to learn (SPEC.md 22)

; -----------------------------------------------------------------------------
; fm_menus - Locator's file-manager menu set (SPEC.md 12.2/12.3/22)
;
; AM_NAME is menu_loc_name, the SAME 'Locator' string the desktop's set
; uses: one application, two menu sets, so the bar's identity never flickers
; as windows come and go. AM_ONCMD is a real handler, which makes this the
; first KERNEL kind on ui_dispatch's .app route - see the note there.
;
; The four titles are laid out at x 110/154/214/258 and end at 325, against
; menu_relayout's MENU_CLK_HX limit of 434: 108px of slack, so no title can
; be silently dropped. Item strings do not enter that arithmetic at all.
; -----------------------------------------------------------------------------
fm_s_mfile:   db 'File', 0
fm_s_mfold:   db 'Folder', 0
fm_s_mview:   db 'View', 0
fm_s_mspec:   db 'Special', 0

fm_s_open:    db 'Open', 0
fm_s_mkdir:   db 'New Folder...', 0
fm_s_closew:  db 'Close Window', 0
fm_s_refr:    db 'Refresh', 0
fm_s_up:      db 'Up One Folder', 0
fm_s_rootf:   db 'Root Folder', 0
fm_s_drva:    db 'Drive A:', 0
fm_s_drvb:    db 'Drive B:', 0
fm_s_aslist:  db 'as List', 0
fm_s_asicon:  db 'as Icons', 0

fm_items_file:
    dw fm_s_open, fm_s_mkdir, fm_s_closew
fm_items_fold:
    dw fm_s_refr, fm_s_up, fm_s_rootf, fm_s_drva, fm_s_drvb
fm_items_view:
    dw fm_s_aslist, fm_s_asicon
fm_items_spec:
    dw menu_s_clock, menu_s_bounce, menu_s_restart

fm_menus:
    dw menu_loc_name            ; AM_NAME: still 'Locator' (SPEC.md 12.3)
    dw fm_oncmd                 ; AM_ONCMD: our own handler
    dw 4                        ; AM_COUNT (= MENU_APPMAX, used exactly)
    dw fm_s_mfile, fm_items_file, 3
    dw fm_s_mfold, fm_items_fold, 5
    dw fm_s_mview, fm_items_view, 2
    dw fm_s_mspec, fm_items_spec, 3

; -----------------------------------------------------------------------------
; files_open - CMD_FILES dispatch entry (ui.inc); no lock held on entry
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; Re-opens the current drive: AL = [disk_drive], fall into files_open_drive
; (SPEC.md 22). Every open remounts - that is the disk-swap story.
; -----------------------------------------------------------------------------
files_open:
    push ax
    mov al, [disk_drive]
    jmp fm_open_body            ; files_open_drive body; pops the AX above

; -----------------------------------------------------------------------------
; files_open_drive - mount a drive and show the Disk window
; in:  AL = drive (0 = A:, 1 = B:); no lock held on entry
; out: nothing
; clobbers: nothing (flags only)
; Callers: CMD_FILES dispatch (via files_open) and desk_click (SPEC.md 26).
; ALWAYS mounts - a swapped or newly chosen disk must never show stale
; contents; fm_mountok records the result for the header line and the
; selection and scroll are cleared. disk_mount runs OUTSIDE the gfx lock
; (SPEC.md 18).
; -----------------------------------------------------------------------------
files_open_drive:
    push ax
fm_open_body:
    push bx
    push dx

    mov dl, al
    call disk_mount             ; sets [disk_drive]; CF = failure
    mov byte [fm_mountok], 0
    jc .fail
    mov byte [fm_mountok], 1
.fail:
    mov word [fm_sel], 0xFFFF   ; directory (re)read: selection is stale
    mov word [fm_scroll], 0     ; and so is the scroll position

    mov al, KIND_FILES          ; create the window, or front + un-minimize
    call app_launch             ; the existing instance (SPEC.md 29)

    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; files_refresh - repaint after a load attempt (loader_run)
; in:  nothing; no lock held on entry
; out: nothing
; clobbers: nothing (flags only)
; No live Disk instance (the user closed it mid-load) = nothing to do; else
; takes the gfx lock itself; hidden window = nothing to do. This must be a
; FULL wm_paint_all, not a content-only repaint: loader_run calls it right
; after wm_show raised the loaded program's window, which may overlap the
; Disk window - painting only our content would draw over the new front
; window. Rare (once per load attempt), so the full repaint is cheap enough.
; -----------------------------------------------------------------------------
files_refresh:
    push ax
    push si
    push di

    mov al, KIND_FILES
    call inst_find_kind         ; DI = the live Disk instance (SPEC.md 29)
    jc .closed
    call gfx_lock
    mov si, [di+I_WIN]
    test word [si+W_FLAGS], 2   ; visible?
    jz .skip
    call wm_paint_all
.skip:
    call gfx_unlock
.closed:

    pop di
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_layout - derive every layout value from the live window record
; in:  BX = window ptr
; out: nothing (all registers preserved); fills the fm_* layout scratch
;      below and clamps fm_scroll into range (SPEC.md 22 "Live layout"):
;        fm_cx/fm_cy    content origin (absolute)
;        fm_cw/fm_ch    content width/height
;        fm_rgt         content right column, absolute, inclusive
;        fm_staty       status line text top, content-relative
;        fm_listb       first row BELOW the row area, content-relative
;        fm_fit         whole rows that fit (0 = row area omitted)
;        fm_cols        grid columns (1 in list view)
;        fm_total       total rows (entries, or grid rows)
;        fm_nch         list-view name chars that fit
; -----------------------------------------------------------------------------
fm_layout:
    push ax
    push cx
    push dx

    call wm_content             ; AX = content left, DX = content top
    mov [fm_cx], ax
    mov [fm_cy], dx
    mov ax, [bx+W_W]
    sub ax, 2
    mov [fm_cw], ax             ; cw = W_W - 2
    add ax, [fm_cx]
    dec ax
    mov [fm_rgt], ax            ; content right, absolute inclusive
    mov ax, [bx+W_H]
    sub ax, TITLE_H+1
    mov [fm_ch], ax             ; ch = W_H - TITLE_H - 1
    sub ax, 17
    mov [fm_staty], ax          ; status line pinned above the bottom
    sub ax, 2
    mov [fm_listb], ax          ; row area ends here

    ; fit = (listb - 22) / row_h, 0 when the window is too short
    sub ax, FM_ROW_Y0
    jg .roompos
    xor ax, ax
    jmp .fit
.roompos:
    mov cx, FM_ROW_H
    cmp byte [fm_view], 0
    je .fitdiv
    mov cx, FMI_CELL_H
.fitdiv:
    xor dx, dx
    div cx
.fit:
    mov [fm_fit], ax

    ; cols = 1 (list) or max(1, (cw-16)/78) (icons)
    mov ax, 1
    cmp byte [fm_view], 0
    je .cols
    mov ax, [fm_cw]
    sub ax, 16
    jle .cols1
    mov cx, FMI_CELL_W
    xor dx, dx
    div cx
    or ax, ax
    jnz .cols
.cols1:
    mov ax, 1
.cols:
    mov [fm_cols], ax

    ; total rows = nfiles (list) or ceil(nfiles / cols) (icons)
    mov ax, [disk_nfiles]
    cmp byte [fm_view], 0
    je .tot
    add ax, [fm_cols]
    dec ax
    xor dx, dx
    div word [fm_cols]
.tot:
    mov [fm_total], ax

    ; list-view name budget: (cw - 88)/8 chars, clamped 0..16 (the name
    ; column must stop short of the size column, SPEC.md 22)
    mov ax, [fm_cw]
    sub ax, 88
    jg .nchpos
    xor ax, ax
    jmp .nchclamp
.nchpos:
    mov cl, 3
    shr ax, cl
.nchclamp:
    cmp ax, 16
    jbe .nch
    mov ax, 16
.nch:
    mov [fm_nch], ax

    call fm_clamp_scroll        ; a shrink-resize self-heals here

    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_clamp_scroll - clamp fm_scroll into [0, max(total - fit, 0)]
; in:  fm_total/fm_fit valid (fm_layout ran)
; out: nothing (all registers preserved)
; Signed on purpose: click paths subtract a page first and clamp after.
; -----------------------------------------------------------------------------
fm_clamp_scroll:
    push ax
    mov ax, [fm_total]
    sub ax, [fm_fit]
    jns .max
    xor ax, ax
.max:
    cmp [fm_scroll], ax
    jle .low
    mov [fm_scroll], ax
.low:
    cmp word [fm_scroll], 0
    jge .done
    mov word [fm_scroll], 0
.done:
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_thumb - scroll-bar thumb geometry (shared by painter and hit-tester)
; in:  fm_* layout valid
; out: CF=1 no thumb (everything fits, or the track is degenerate); else
;      CF=0, AX = thumb top (absolute y), DX = thumb height
; clobbers: nothing else
; Track = content-relative rows 33 .. listb-12 (between the arrow rules),
; height listb-44. thumb h = max(8, fit*track/total), top rides fm_scroll.
; -----------------------------------------------------------------------------
fm_thumb:
    push bx
    push cx
    mov ax, [fm_total]
    cmp ax, [fm_fit]
    jbe .none
    mov bx, [fm_listb]
    sub bx, 44                  ; BX = track height
    cmp bx, 8                   ; shorter than the minimum thumb: bare
    jl .none                    ; track (SPEC.md 22 degenerate exception)
    mov ax, [fm_fit]
    mul bx
    div word [fm_total]         ; AX = proportional thumb height
    cmp ax, 8
    jge .hok
    mov ax, 8
.hok:
    mov cx, ax                  ; CX = thumb height
    mov ax, [fm_scroll]
    mul bx
    div word [fm_total]         ; AX = thumb offset in the track
    add ax, FM_ROW_Y0+11        ; track top, content-relative (= 33)
    mov dx, bx                  ; clamp: top <= track top + track - height
    add dx, FM_ROW_Y0+11
    sub dx, cx
    cmp ax, dx
    jle .tok
    mov ax, dx
.tok:
    add ax, [fm_cy]             ; absolute
    mov dx, cx
    clc
    jmp .out
.none:
    stc
.out:
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; fm_paint - W_PAINT for the Disk window
; in:  SI = window ptr (content already white-filled, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; -----------------------------------------------------------------------------
fm_paint:
    push bx
    mov bx, si
    call fm_draw_core
    pop bx
    ret

; -----------------------------------------------------------------------------
; fm_onclick - W_ONCLICK for the Disk window
; in:  CX = x, DX = y (absolute), SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; Test order (SPEC.md 22): buttons -> scroll bar -> rows. Buttons are
; tested only when wide enough to be drawn (the same cw guards as the
; painter - nothing invisible is ever clickable). fm_sel holds a DIRECTORY
; index; the row math adds fm_scroll and, in icon view, the column.
;
; A click anywhere cancels an in-place name (SPEC.md 22), on the reasoning
; that clicking away from a rename is how every desktop abandons one - and
; because leaving the editor armed would swallow the next keystroke too.
; -----------------------------------------------------------------------------
fm_onclick:
    push ax
    push bx
    push cx
    push dx
    push di

    call fm_edit_end            ; clicking away abandons a half-typed name

    mov bx, si
    push cx                     ; save click x
    mov cx, dx                  ; CX = click y
    call wm_content             ; AX = content left, DX = content top
    sub cx, dx                  ; CX = content-relative y
    pop dx
    sub dx, ax                  ; DX = content-relative x
    call fm_layout              ; BX = window ptr

    ; --- Refresh button (drawn iff cw >= 76) ---
    mov ax, [fm_cw]
    cmp ax, 76
    jl .viewbtn
    sub ax, 68                  ; x1 = cw-68 .. x2 = cw-6
    cmp dx, ax
    jl .viewbtn
    add ax, 62
    cmp dx, ax
    jg .viewbtn
    cmp cx, 2
    jl .viewbtn
    cmp cx, 15
    jg .viewbtn
    mov dl, [disk_drive]        ; remount the current drive in place
    call fm_mount               ; (under the gfx lock: stalls painters
    jmp .redraw                 ;  briefly; accepted by SPEC.md 22)

    ; --- view toggle (drawn iff cw >= 142) ---
.viewbtn:
    mov ax, [fm_cw]
    cmp ax, 142
    jl .sbar
    sub ax, 136                 ; x1 = cw-136 .. x2 = cw-74
    cmp dx, ax
    jl .sbar
    add ax, 62
    cmp dx, ax
    jg .sbar
    cmp cx, 2
    jl .sbar
    cmp cx, 15
    jg .sbar
    xor byte [fm_view], 1       ; flip the view, back to the top; the
    mov word [fm_scroll], 0     ; selection is kept (SPEC.md 22)
    jmp .redraw

    ; --- scroll bar (present iff the row area is) ---
.sbar:
    cmp word [fm_fit], 0
    je .rows
    mov ax, [fm_cw]
    sub ax, FM_SB_W
    cmp dx, ax
    jl .rows                    ; left of the bar
    cmp cx, FM_ROW_Y0
    jl .rows                    ; above it
    cmp cx, [fm_listb]
    jge .rows                   ; below it
    cmp cx, FM_ROW_Y0+10        ; up-arrow cell, rows 22..32
    jle .sb_up
    mov ax, [fm_listb]
    sub ax, 11                  ; down-arrow cell, rows listb-11..listb-1
    cmp cx, ax
    jge .sb_down
    call fm_thumb               ; track: page against the thumb top
    jc .redraw                  ; no thumb: the track is inert
    sub ax, [fm_cy]             ; thumb top, content-relative
    cmp cx, ax
    jl .sb_pgup
    mov ax, [fm_fit]            ; at/below the thumb top: page down
    add [fm_scroll], ax
    jmp .sb_clamp
.sb_pgup:
    mov ax, [fm_fit]
    sub [fm_scroll], ax
    jmp .sb_clamp
.sb_up:
    dec word [fm_scroll]
    jmp .sb_clamp
.sb_down:
    inc word [fm_scroll]
.sb_clamp:
    call fm_clamp_scroll
    jmp .redraw

    ; --- the row area ---
.rows:
    mov di, dx                  ; DI = content-relative x
    mov ax, cx
    sub ax, FM_ROW_Y0           ; rebase y to row 0
    js .clear                   ; header band clears the selection
    mov cx, FM_ROW_H
    cmp byte [fm_view], 0
    je .rowdiv
    mov cx, FMI_CELL_H
.rowdiv:
    xor dx, dx
    div cx                      ; AX = visible row
    cmp ax, [fm_fit]
    jae .clear                  ; below the row area
    cmp byte [fm_view], 0
    je .listidx
    push ax                     ; icon view: column from x
    mov ax, di
    mov cx, FMI_CELL_W
    xor dx, dx
    div cx
    mov di, ax                  ; DI = column
    pop ax
    cmp di, [fm_cols]
    jae .clear                  ; right of the last column
    add ax, [fm_scroll]
    mul word [fm_cols]
    add ax, di                  ; AX = directory index
    jmp .chk
.listidx:
    add ax, [fm_scroll]         ; AX = directory index
.chk:
    cmp ax, [disk_nfiles]
    jae .clear                  ; past the directory

    cmp ax, [fm_sel]            ; second click on the selected entry?
    jne .select
    mov dx, [ui_click_t]        ; click birth tick (EV_C), not [ticks]:
    sub dx, [fm_clkt]           ; wrap-safe (SPEC.md 8); queued clicks
                                ; must not collapse into double-clicks
    cmp dx, FM_DBLCLK
    jae .select
    call fm_open_sel            ; double-click: a package goes to the
    jmp .redraw                 ; loader, a folder to dsk_chdir (22)

.select:
    mov [fm_sel], ax
    mov dx, [ui_click_t]
    mov [fm_clkt], dx
    jmp .redraw

.clear:
    mov word [fm_sel], 0xFFFF

.redraw:
    call fm_repaint             ; SI still = window ptr

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_onkey - W_ONKEY for the Disk window
; in:  AL = ascii, AH = scan, SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; While [fm_edit] is set this handler SWALLOWS EVERY KEY and none of the
; shortcuts below apply (SPEC.md 22). That is binding, not defensive: with
; the bare-letter shortcuts live, typing a folder named 'BAK' would switch
; to drive B: on its very first character and the name would be half-eaten.
;
; Otherwise: 'a'/'A' mounts drive 0, 'b'/'B' drive 1, 'r'/'R' remounts the
; current drive (disk_mount under the gfx lock stalls painters briefly;
; accepted by SPEC.md 22). 'v'/'V' toggles the view. Up/Down/PgUp/PgDn (scan
; 48h/50h/49h/51h) scroll the view. Enter opens the selection like a
; double-click.
; -----------------------------------------------------------------------------
fm_onkey:
    push ax
    push bx
    push dx

    cmp byte [fm_edit], 0
    jne .editing                ; naming: nothing below is reachable

    cmp al, 13
    je .enter
    cmp al, 'v'
    je .vtoggle
    cmp al, 'V'
    je .vtoggle
    mov dl, 0
    cmp al, 'a'
    je .mount
    cmp al, 'A'
    je .mount
    mov dl, 1
    cmp al, 'b'
    je .mount
    cmp al, 'B'
    je .mount
    cmp al, 'r'
    je .remount
    cmp al, 'R'
    je .remount
    cmp ah, 0x48                ; Up
    je .scr_up
    cmp ah, 0x50                ; Down
    je .scr_dn
    cmp ah, 0x49                ; PgUp
    je .scr_pgup
    cmp ah, 0x51                ; PgDn
    je .scr_pgdn
    jmp .out

.editing:
    or al, al
    jz .out                     ; a bare scan code: swallowed, nothing moved
    call fm_editkey
    jmp .redraw

.remount:
    mov dl, [disk_drive]

.mount:
    call fm_mount               ; DL = drive; the same body the Refresh
    jmp .redraw                 ; button and the Folder menu use

.vtoggle:
    xor byte [fm_view], 1
    mov word [fm_scroll], 0
    jmp .redraw

.scr_up:
    mov bx, si                  ; the scroll keys clamp against the live
    call fm_layout              ; layout (fm_fit/fm_total)
    dec word [fm_scroll]
    jmp .scr_clamp
.scr_dn:
    mov bx, si
    call fm_layout
    inc word [fm_scroll]
    jmp .scr_clamp
.scr_pgup:
    mov bx, si
    call fm_layout
    mov ax, [fm_fit]
    sub [fm_scroll], ax
    jmp .scr_clamp
.scr_pgdn:
    mov bx, si
    call fm_layout
    mov ax, [fm_fit]
    add [fm_scroll], ax
.scr_clamp:
    call fm_clamp_scroll
    jmp .redraw

.enter:
    mov ax, [fm_sel]
    cmp ax, [disk_nfiles]       ; 0xFFFF (none) also fails this
    jae .out
    call fm_open_sel            ; same as a double-click

.redraw:
    call fm_repaint             ; SI still = window ptr

.out:
    pop dx
    pop bx
    pop ax
    ret

; =============================================================================
; Navigation and the shared open path (SPEC.md 19.2/22)
; =============================================================================

; -----------------------------------------------------------------------------
; fm_open_sel - open one directory entry: THE open path (SPEC.md 22)
; in:  AX = directory index, already checked against [disk_nfiles];
;      gfx lock held
; out: nothing (all registers preserved)
;
; Shared by the double-click, by Enter and by File > Open, so those three can
; never drift apart. The SPEC.md 19 type word decides: 2 is a folder and gets
; dsk_chdir; anything else goes to the loader, which rejects a type-0 file as
; "Bad package" - the truthful verdict for double-clicking a data file.
;
; Before this branch existed a folder double-click reached ld_run_body, which
; rejects type != 1, and the window reported "Bad package" for an operation
; that had nothing to do with packages.
;
; Both the cluster and the name are taken out of the staged entry BEFORE
; anything acts: fm_go remounts, and a mount rebuilds dsk_ent - the very
; buffer dsk_get_dir just handed back a pointer into.
; -----------------------------------------------------------------------------
fm_open_sel:
    push ax
    push bx
    push si
    call dsk_get_dir            ; SI -> the staged 32-byte entry
    mov bx, [si+18]             ; its first cluster, banked before the mount
    cmp word [si+16], 2
    je .folder
    inc ax                      ; index + 1: ui.inc runs the loader once the
    mov [ld_pending], ax        ; lock drops (SPEC.md 21)
    jmp short .out
.folder:
    call fm_stage_name          ; fm_name = the folder's display name, out
    mov ax, bx                  ; of the staged entry while it still exists
    mov si, fm_name
    call fm_go
.out:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_go - move the window to a directory (SPEC.md 19.2/22)
; in:  AX = the directory's first cluster (0 = the root), SI = a NUL display
;      name to title the window with, or 0 = derive it; gfx lock held
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fm_go:
    push dx
    mov dl, [disk_drive]
    call dsk_chdir              ; a remount with [dsk_keepcwd] raised; CF=1
    call fm_relist              ; leaves the volume back at the root (19.2)
    pop dx
    ret

; -----------------------------------------------------------------------------
; fm_mount - mount a drive from scratch (SPEC.md 22)
; in:  DL = drive (0 = A:, 1 = B:); gfx lock held
; out: nothing (all registers preserved)
;
; The body behind the Refresh button, the a/b/r keys and the Folder menu's
; Refresh / Drive A: / Drive B: - one copy, so a drive change can never mean
; three slightly different things. Unlike fm_go this is NOT navigation:
; [dsk_keepcwd] stays clear, so the volume lands at the root, deliberately -
; a swapped disk has no meaningful "current folder".
; -----------------------------------------------------------------------------
fm_mount:
    push si
    call disk_mount             ; sets [disk_drive]; CF = failure
    mov si, 0                   ; back at the root: the title derives. NOT
                                ; `xor si,si` - that clears CF, which is
                                ; fm_relist's input and the mount's verdict
    call fm_relist
    pop si
    ret

; -----------------------------------------------------------------------------
; fm_relist - the tail every (re)listing shares (module-internal)
; in:  CF = the mount's verdict, SI = a NUL title name or 0 to derive one
; out: nothing (all registers preserved)
;
; Everything the window shows is a function of the current listing, so one
; routine resets all of it: the flag the header reads, the selection and the
; scroll position (both are indices into a listing that has just been
; replaced), the stale error text, and the title.
; -----------------------------------------------------------------------------
fm_relist:
    mov byte [fm_mountok], 0    ; mov does not touch CF, so the carry the
    jc .listed                  ; caller handed us is still live here
    mov byte [fm_mountok], 1
.listed:
    mov word [fm_sel], 0xFFFF
    mov word [fm_scroll], 0
    mov byte [fm_ferr], 0
    jmp fm_settitle             ; tail call; preserves everything

; -----------------------------------------------------------------------------
; fm_settitle - point the window's caption at where the volume now is
; in:  SI = a NUL display name, or 0
; out: nothing (all registers preserved)
;
; [dsk_cwd] is the authority, not SI. A failed dsk_chdir leaves the volume at
; the root (SPEC.md 19.2), and titling the window with the folder we failed
; to ENTER would be a lie the user could act on - so the root always reads
; 'Disk'. Inside a folder SI names it when the caller knows the name, and
; 'Folder' when it does not, which is only the Up case: naming that
; directory would need the GRANDparent's listing, i.e. a second whole mount
; to display a string (SPEC.md 22).
;
; The caption, the dock tile and the Task Manager row are the same 16 bytes
; (the instance's I_NAME, aimed at by fm_kinit), so all three follow.
; -----------------------------------------------------------------------------
fm_settitle:
    push ax
    push cx
    push si
    push di
    mov di, [fm_titlep]
    or di, di
    jz .out                     ; no window yet: nothing to retitle
    cmp word [dsk_cwd], 0
    jne .named
    mov si, fm_s_root
    jmp short .copy
.named:
    or si, si
    jnz .copy
    mov si, fm_s_folder
.copy:
    mov byte [fm_full], 1       ; the caption lives in the FRAME, which a
                                ; content repaint never touches: ask the
                                ; next fm_repaint for the whole screen
    mov cx, 15                  ; I_NAME is 16 bytes with a permanent NUL
.ch:                            ; at byte 15 (SPEC.md 29.1)
    lodsb                       ; DF=0 per SPEC.md 1
    mov [di], al
    inc di
    or al, al
    jz .out
    loop .ch
    mov byte [di], 0
.out:
    pop di
    pop si
    pop cx
    pop ax
    ret

; =============================================================================
; The menu bar: fm_menus' command handler (SPEC.md 12.2/22)
; =============================================================================

; --- command ids -------------------------------------------------------------
; Contiguous WITHIN a menu, which is what makes fm_oncmd's base + item
; arithmetic work. They are internal constants with no on-disk or cross-
; module meaning, so a menu that gains an item simply renumbers what follows.
FMC_OPEN    equ 0               ; File
FMC_MKDIR   equ 1
FMC_CLOSEW  equ 2
FMC_REFRESH equ 3               ; Folder
FMC_UP      equ 4
FMC_ROOT    equ 5
FMC_DRVA    equ 6
FMC_DRVB    equ 7
FMC_LIST    equ 8               ; View
FMC_ICONS   equ 9
FMC_CLOCK   equ 10              ; Special
FMC_BOUNCE  equ 11
FMC_RESTART equ 12
FMC_COUNT   equ 13

FM_NMENUS   equ 4               ; = fm_menus' AM_COUNT = MENU_APPMAX
fm_menu_base:
    db FMC_OPEN, FMC_REFRESH, FMC_LIST, FMC_CLOCK

; The jump table. Every entry records WHICH SIDE OF THE GFX LOCK it is on,
; because fm_oncmd runs under that lock and the lock is a non-recursive spin
; only the UI task releases (SPEC.md 7): an entry that called app_launch,
; files_open, files_refresh or ui_cmd would hang the machine permanently -
; no beep, no watchdog, no recovery. This is the single easiest mistake to
; make in this module, which is why the note is per line and not per file.
fm_jmp:
    dw fm_c_open                ; inline (the loader is deferred; chdir is
                                ;         disk I/O under the lock, like
                                ;         Refresh already is)
    dw fm_c_mkdir               ; inline (arms the status-line editor)
    dw fm_c_closew              ; POSTED - ui_cmd takes the lock
    dw fm_c_refresh             ; inline (disk_mount, like the button)
    dw fm_c_up                  ; inline (dsk_dotdot + dsk_chdir)
    dw fm_c_root                ; inline (dsk_chdir AX = 0)
    dw fm_c_drva                ; inline (disk_mount DL = 0)
    dw fm_c_drvb                ; inline (disk_mount DL = 1)
    dw fm_c_list                ; inline ([fm_view] = 0)
    dw fm_c_icons               ; inline ([fm_view] = 1)
    dw fm_c_clock               ; POSTED - app_launch takes the lock
    dw fm_c_bounce              ; POSTED - app_launch takes the lock
    dw fm_c_restart             ; POSTED - ui_cmd takes the lock, forever

; -----------------------------------------------------------------------------
; fm_oncmd - AM_ONCMD for fm_menus (SPEC.md 12.2)
; in:  AL = item index, AH = menu index (0-based), SI = our window ptr,
;      BX = the menu set. gfx lock HELD, on the UI task, billed to the
;      instance - the W_ONCLICK contract verbatim.
; out: nothing (all registers preserved)
;
; (menu, item) collapses to one FMC_* and one near-indirect call. A single id
; space rather than a per-menu ladder is deliberate: it is what will let a
; context menu fire the same commands later without a second implementation
; of any of them.
;
; The window pointer is parked in .bss rather than kept in SI, because the
; handlers stage directory entries and call the disk, and SI is the staging
; register everywhere in this module. The closing repaint is NOT optional -
; the kernel does not repaint after an AM_ONCMD returns (SPEC.md 12.2).
; -----------------------------------------------------------------------------
fm_oncmd:
    push ax
    push bx
    push cx
    push dx
    push si
    push di

    mov [fm_cmdwin], si
    mov bl, ah
    xor bh, bh
    cmp bx, FM_NMENUS
    jae .out                    ; menu_relayout clamps the cell index to
                                ; AM_COUNT, so this cannot fire - it is what
                                ; keeps the table read in range anyway
    mov bl, [fm_menu_base+bx]   ; the menu's first id...
    add bl, al                  ; ...plus the item (menu_hover bounds AL by
                                ; the menu's own item count, SPEC.md 12)
    cmp bx, FMC_COUNT
    jae .out
    shl bx, 1
    call [fm_jmp+bx]            ; CALL r/m16 near-indirect: 8086-legal

    mov si, [fm_cmdwin]
    call fm_repaint
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; --- the command bodies ------------------------------------------------------
; Each is near-called by fm_oncmd with the gfx lock held, may clobber
; anything (fm_oncmd saved it all), and draws nothing: fm_oncmd repaints.

fm_c_open:                      ; File > Open - the selection, if any
    mov ax, [fm_sel]
    cmp ax, [disk_nfiles]       ; 0xFFFF (no selection) also fails this
    jae .none
    call fm_open_sel
.none:
    ret

fm_c_mkdir:                     ; File > New Folder... - arm the editor
    mov byte [fm_edit], 1
    mov byte [fm_elen], 0
    mov byte [fm_ebuf], 0
    mov byte [fm_ferr], 0       ; the prompt replaces the old verdict
    ret

fm_c_closew:                    ; File > Close Window
    mov ax, CMD_CLOSE
    jmp ui_post_cmd             ; NOT ui_cmd: it takes the lock we hold

fm_c_restart:                   ; Special > Restart
    mov ax, CMD_REBOOT
    jmp ui_post_cmd

fm_c_clock:                     ; Special > Clock
    mov al, KIND_CLOCK
    jmp inst_launch_post        ; NOT app_launch: it takes the lock (29.4)

fm_c_bounce:                    ; Special > Bounce
    mov al, KIND_BOUNCE
    jmp inst_launch_post

fm_c_refresh:                   ; Folder > Refresh
    mov dl, [disk_drive]
    jmp short fm_c_mount
fm_c_drva:                      ; Folder > Drive A:
    xor dl, dl
    jmp short fm_c_mount
fm_c_drvb:                      ; Folder > Drive B:
    mov dl, 1
fm_c_mount:
    jmp fm_mount

fm_c_up:                        ; Folder > Up One Folder
    cmp word [dsk_cwd], 0
    je .none                    ; already at the root: nothing is above it,
                                ; and saying so is not worth an error line
    call dsk_dotdot             ; AX = the parent's first cluster (19.2)
    jc .none                    ; a missing or corrupt '..': stay put
    xor si, si                  ; the parent cannot be named without the
    jmp fm_go                   ; grandparent's listing (SPEC.md 22)
.none:
    ret

fm_c_root:                      ; Folder > Root Folder
    xor ax, ax
    xor si, si
    jmp fm_go

fm_c_list:                      ; View > as List
    xor al, al
    jmp short fm_c_setview
fm_c_icons:                     ; View > as Icons
    mov al, 1
fm_c_setview:
    mov [fm_view], al
    mov word [fm_scroll], 0     ; row heights changed: the old first row is
    ret                         ; not this view's first row

; =============================================================================
; The status-line name editor (SPEC.md 22)
; =============================================================================

; -----------------------------------------------------------------------------
; fm_editkey - one keystroke while [fm_edit] is armed
; in:  AL = ascii (non-zero); gfx lock held
; out: nothing (all registers preserved)
;
; Characters are filtered through diskw.inc's OWN dskw_char - the same
; routine dskw_name83 will apply at commit time - so the line shows exactly
; the bytes that will be stored and a name can never look accepted and then
; come back FERR_NAME.
; -----------------------------------------------------------------------------
fm_editkey:
    push ax
    push bx
    cmp al, 13
    je .commit
    cmp al, 27
    je .cancel
    cmp al, 8
    je .back

    mov bl, [fm_elen]
    xor bh, bh
    cmp bl, 12
    jae .out                    ; 8 + '.' + 3 is the whole 8.3 budget
    cmp al, '.'
    jne .ordinary
    or bl, bl
    jz .out                     ; a short name may not start with the dot
    call fm_hasdot
    jc .out                     ; ...and may not carry a second one
    jmp short .store
.ordinary:
    call dskw_char              ; upper-cases, or CF=1 = not in the FAT set
    jc .out
.store:
    mov [fm_ebuf+bx], al
    inc bx
    mov byte [fm_ebuf+bx], 0
    mov [fm_elen], bl
    jmp short .out

.back:
    mov bl, [fm_elen]
    xor bh, bh
    or bl, bl
    jz .out
    dec bl
    mov [fm_elen], bl
    mov byte [fm_ebuf+bx-1], 0
    jmp short .out

.cancel:
    call fm_edit_end
    jmp short .out
.commit:
    call fm_edit_commit
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_hasdot - does the name being typed already carry a '.'? (internal)
; in:  nothing
; out: CF=1 if it does; all registers preserved
; -----------------------------------------------------------------------------
fm_hasdot:
    push si
    mov si, fm_ebuf
.scan:
    cmp byte [si], 0
    je .no
    cmp byte [si], '.'
    je .yes
    inc si
    jmp short .scan
.yes:
    pop si
    stc
    ret
.no:
    pop si
    clc
    ret

; -----------------------------------------------------------------------------
; fm_edit_commit - Enter in the editor: create the folder (SPEC.md 18.5)
; in:  nothing; gfx lock held, UI task - dskw_*'s required context (18.4)
; out: nothing (all registers preserved); the verdict lands in [fm_ferr]
;
; dskw_mkdir remounts on success with [dsk_cwd] preserved, so the new folder
; appears in the listing and the volume stays exactly where it was - which is
; the entire point of being able to create a folder inside another one.
; -----------------------------------------------------------------------------
fm_edit_commit:
    push ax
    push si
    cmp byte [fm_elen], 0
    je .done                    ; nothing typed: Enter simply cancels
    mov si, fm_ebuf
    call dskw_mkdir             ; CF=1 with AX = FERR_* (SPEC.md 18.5)
    jnc .ok
    mov [fm_ferr], al           ; every failure gets visible text (22)
    jmp short .done
.ok:
    mov byte [fm_ferr], 0
    mov word [fm_sel], 0xFFFF   ; the remount rebuilt the listing, so a
    mov word [fm_scroll], 0     ; directory index means nothing any more
.done:
    call fm_edit_end
    pop si
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_edit_end - leave the editor (internal; also the "cancel" path)
; in:  nothing
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fm_edit_end:
    mov byte [fm_edit], 0
    mov byte [fm_elen], 0
    mov byte [fm_ebuf], 0
    ret

; -----------------------------------------------------------------------------
; fm_repaint - white-fill own content, then redraw everything
; in:  SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; The Note Pad onkey idiom (SPEC.md 22): W_PAINT gets a pre-filled content
; from wm_paint_all, but self-initiated repaints must fill it themselves -
; from the LIVE record: the window resizes (SPEC.md 11.1).
;
; [fm_full] escalates to the whole screen, and only navigation raises it
; (fm_settitle): the window's CAPTION lives in the frame, which a content
; repaint never touches, so a folder change would otherwise leave the title
; bar naming the folder we just left. wm_paint_all under the caller's held
; lock is the ui_drag idiom (SPEC.md 13), and it is affordable precisely
; because the only thing that raises the flag has just paid for a whole
; disk mount - 16 sector reads that dwarf any repaint.
; -----------------------------------------------------------------------------
fm_repaint:
    push ax
    push bx
    push cx
    push dx

    cmp byte [fm_full], 0
    je .content
    mov byte [fm_full], 0
    call wm_paint_all           ; the frame too: the caption just changed
    jmp .done
.content:

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
    call gfx_fill               ; white-fill content

    mov bx, si
    call fm_draw_core
    call wm_grow_paint          ; the white fill above erased the grow box;
                                ; BX is still the window ptr (SPEC.md 11.1)
.done:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_draw_icon16 - one entry's 16x16 icon, with the generic fallback
; in:  AX = directory index, CX = x, DX = y (absolute, gfx lock held)
; out: nothing (all registers preserved)
; dsk_get_icon stages the entry from LOW_SEG (SPEC.md 19); an all-zero
; entry (the 32 words OR to 0) falls back to the built-in ico_app16.
; -----------------------------------------------------------------------------
fm_draw_icon16:
    push ax
    push bx
    push cx
    push si
    call dsk_get_icon           ; SI -> staged 64-byte icon body
    mov bx, si
    push cx                     ; the x coordinate must survive the loop
    xor ax, ax                  ; counter below
    mov cx, 32
.orwords:
    or ax, [bx]
    inc bx
    inc bx
    loop .orwords
    pop cx
    test ax, ax
    jz .noicon
    call icon_draw16            ; body-only table entry (SPEC.md 25)
    jmp .done
.noicon:
    mov si, ico_app16           ; the built-in app icon (library record)
    call icon_draw
.done:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_stage_name - stage a directory entry's name, type and size
; in:  AX = directory index
; out: fm_name = NUL-terminated copy of the 16-byte name field (which is
;      NUL-PADDED, not NUL-terminated - the copy forces a terminator),
;      [fm_ftype] = the SPEC.md 19 type word, [fm_fsize] = size in bytes.
;      All registers preserved.
; The type comes out with the name because both callers need it and the
; entry is staged only once: fm_open_sel branches on it and the list row
; suppresses the size column for a folder.
; -----------------------------------------------------------------------------
fm_stage_name:
    push ax
    push bx
    push cx
    push si
    call dsk_get_dir            ; SI -> staged 32-byte entry (SPEC.md 19)
    mov bx, fm_name
    mov cx, 16
.copy:
    lodsb                       ; DF=0 on entry per SPEC.md 1
    mov [bx], al
    inc bx
    loop .copy
    mov byte [fm_name+16], 0
    mov ax, [si]                ; type word (entry offset 16; SI advanced
    mov [fm_ftype], ax          ; past the name)
    mov ax, [si+4]              ; size word (entry offset 20)
    mov [fm_fsize], ax
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_draw_core - draw the whole content (header, buttons, rows or grid,
; scroll bar, selection, status)
; in:  BX = window ptr (content already white, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Layout comes from fm_layout - nothing here may assume 320x200 (SPEC.md
; 22). Row area and scroll bar are omitted when fm_fit = 0; each button
; is drawn only when it fits (Refresh: cw >= 76, view toggle: cw >= 142),
; and the header is truncated to stop 8px short of the leftmost button.
; -----------------------------------------------------------------------------
fm_draw_core:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call fm_layout

    ; --- header line ---
    mov di, fm_hdrbuf
    cmp byte [fm_mountok], 0
    je .badhdr
    mov si, fm_s_drive          ; "Drive X:  N files"
    call fm_scat
    mov al, [disk_drive]
    add al, 'A'
    mov [di], al
    inc di
    mov si, fm_s_colsp
    call fm_scat
    mov ax, [disk_nfiles]
    call fm_utoa
    mov si, fm_s_files
    call fm_scat
    jmp .hdrtrunc
.badhdr:
    mov si, fm_s_nodisk         ; "No os8088 disk (X:)"
    call fm_scat
    mov al, [disk_drive]
    add al, 'A'
    mov [di], al
    inc di
    mov byte [di], ':'
    inc di
    mov byte [di], ')'
    inc di
    mov byte [di], 0
.hdrtrunc:
    ; truncate to (leftmost drawn button - 8)/8 chars (SPEC.md 22)
    mov ax, [fm_cw]
    cmp ax, 142
    jl .hnoview
    sub ax, 136
    jmp .hdrcut
.hnoview:
    cmp ax, 76
    jl .hdrcut                  ; no buttons: bound by cw itself
    sub ax, 68
.hdrcut:
    sub ax, 14                  ; text starts at x=6, so ending 8px short
    jg .hdrpos                  ; of the button means chars <= (b-14)/8
    xor ax, ax
.hdrpos:
    mov cl, 3
    shr ax, cl                  ; AX = chars that fit
    cmp ax, 25
    jae .drawhdr                ; buffer is 26 bytes; never poke past it
    mov si, ax
    mov byte [fm_hdrbuf+si], 0
.drawhdr:
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov dx, [fm_cy]
    add dx, 6
    mov si, fm_hdrbuf
    call font_str

    ; --- Refresh button (iff cw >= 76): frame (cw-68,2)..(cw-6,15) ---
    cmp word [fm_cw], 76
    jl .nobtns
    mov ax, [fm_rgt]
    sub ax, 67                  ; abs x1 = cx + cw - 68 = rgt - 67
    mov bx, [fm_cy]
    add bx, 2
    mov cx, [fm_rgt]
    sub cx, 5                   ; abs x2 = rgt - 5
    mov dx, [fm_cy]
    add dx, 15
    call gfx_frame              ; color still CBLACK from the header
    mov cx, ax
    add cx, 3                   ; 56px label centered in the 63px rect
    mov dx, [fm_cy]
    add dx, 4
    mov si, fm_s_refresh
    call font_str

    ; --- view toggle (iff cw >= 142): frame (cw-136,2)..(cw-74,15) ---
    cmp word [fm_cw], 142
    jl .nobtns
    mov ax, [fm_rgt]
    sub ax, 135                 ; abs x1 = rgt - 135
    mov bx, [fm_cy]
    add bx, 2
    mov cx, [fm_rgt]
    sub cx, 73                  ; abs x2 = rgt - 73
    mov dx, [fm_cy]
    add dx, 15
    call gfx_frame
    mov cx, ax
    add cx, 11                  ; "Icons" = 40px -> x1+11
    mov si, fm_s_icons
    cmp byte [fm_view], 0
    je .vlabel
    add cx, 4                   ; "List" = 32px -> x1+15
    mov si, fm_s_list
.vlabel:
    mov dx, [fm_cy]
    add dx, 4
    call font_str
.nobtns:

    ; --- rows / grid + scroll bar (only when at least one row fits) ---
    cmp word [fm_fit], 0
    je .norows

    mov ax, [fm_total]          ; shown = min(total - scroll, fit)
    sub ax, [fm_scroll]
    jns .sh
    xor ax, ax
.sh:
    cmp ax, [fm_fit]
    jbe .shok
    mov ax, [fm_fit]
.shok:
    mov [fm_shown], ax

    cmp byte [fm_view], 0
    jne .icons

    ; --- list rows ---
    xor di, di                  ; DI = visible row index
.lrow:
    cmp di, [fm_shown]
    jae .lsel
    mov ax, FM_ROW_H            ; BP = row band top (absolute)
    mul di
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bp, ax
    mov ax, di
    add ax, [fm_scroll]         ; AX = directory index

    push ax
    mov cx, [fm_cx]             ; 16x16 icon at x=4, row top
    add cx, 4
    mov dx, bp
    call fm_draw_icon16
    pop ax
    call fm_stage_name          ; fm_name + [fm_fsize]

    mov bx, [fm_nch]            ; truncate the name to the live budget
    cmp bx, 16
    jae .lname
    mov byte [fm_name+bx], 0
.lname:
    add bp, 4                   ; text y: row top + 4 centers 8px glyphs
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 24
    mov dx, bp
    mov si, fm_name
    call font_str               ; name at x = 24

    cmp word [fm_ftype], 2      ; a folder has no meaningful size (19), and
    je .lnosize                 ; a column of zeroes would only read as one
    mov ax, [fm_fsize]
    push di
    mov di, fm_numbuf
    call fm_utoa
    pop di
    mov si, fm_numbuf
    call font_width             ; AX = string width, px
    mov cx, [fm_rgt]            ; right-align ending at cw-22 (clear of
    sub cx, 20                  ; the scroll bar, SPEC.md 22)
    sub cx, ax
    mov dx, bp
    call font_str
.lnosize:

    inc di
    jmp .lrow

.lsel:
    ; selection bar (inverted row band), only when scrolled into view
    mov ax, [fm_sel]
    cmp ax, [disk_nfiles]       ; 0xFFFF (none) also fails this
    jae .bar
    sub ax, [fm_scroll]
    js .bar
    cmp ax, [fm_shown]
    jae .bar
    mov cx, FM_ROW_H
    mul cx
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bx, ax                  ; y1 = row band top
    mov dx, ax
    add dx, FM_ROW_H - 1        ; y2
    mov ax, [fm_cx]             ; row area width: x 0 .. cw-16
    mov cx, [fm_rgt]
    sub cx, 15
    call gfx_xor_fill
    jmp .bar

    ; --- icon grid ---
.icons:
    mov word [fm_vrow], 0
.irow:
    mov ax, [fm_vrow]
    cmp ax, [fm_shown]
    jae .isel
    mov word [fm_vcol], 0
.icell:
    mov ax, [fm_vrow]
    add ax, [fm_scroll]
    mul word [fm_cols]
    add ax, [fm_vcol]           ; AX = directory index
    cmp ax, [disk_nfiles]
    jae .isel                   ; past the last entry: grid done
    push ax
    mov ax, FMI_CELL_W
    mul word [fm_vcol]
    add ax, [fm_cx]
    mov [fm_cellx], ax
    mov ax, FMI_CELL_H
    mul word [fm_vrow]
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov [fm_celly], ax
    pop ax

    push ax
    mov cx, [fm_cellx]          ; icon centered at cell +(31, 3)
    add cx, 31
    mov dx, [fm_celly]
    add dx, 3
    call fm_draw_icon16
    pop ax
    call fm_stage_name
    mov byte [fm_name+9], 0     ; grid names: 9 chars max (SPEC.md 22)
    mov si, fm_name
    call font_width
    mov cx, FMI_CELL_W
    sub cx, ax
    shr cx, 1                   ; centered under the icon
    add cx, [fm_cellx]
    mov dx, [fm_celly]
    add dx, 23
    mov byte [gfx_color], CBLACK
    call font_str

    inc word [fm_vcol]
    mov ax, [fm_vcol]
    cmp ax, [fm_cols]
    jb .icell
    inc word [fm_vrow]
    jmp .irow

.isel:
    ; selection cell (inverted), only when scrolled into view
    mov ax, [fm_sel]
    cmp ax, [disk_nfiles]
    jae .bar
    xor dx, dx
    div word [fm_cols]          ; AX = grid row, DX = column
    mov cx, dx                  ; CX = column
    sub ax, [fm_scroll]
    js .bar
    cmp ax, [fm_shown]
    jae .bar
    push ax
    mov ax, FMI_CELL_W
    mul cx
    add ax, [fm_cx]
    mov [fm_cellx], ax
    pop ax
    mov cx, FMI_CELL_H
    mul cx
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bx, ax                  ; y1 = cell top
    mov dx, ax
    add dx, FMI_CELL_H - 1
    mov ax, [fm_cellx]
    mov cx, ax
    add cx, FMI_CELL_W - 1
    call gfx_xor_fill

    ; --- scroll bar: x cw-14..cw-1, y 22..listb-1 (SPEC.md 22) ---
.bar:
    mov ax, [fm_rgt]
    sub ax, FM_SB_W-1           ; abs x1
    mov [fm_sbx1], ax
    mov bx, [fm_cy]
    add bx, FM_ROW_Y0
    mov [fm_sby1], bx           ; abs y1
    mov cx, [fm_rgt]            ; abs x2
    mov dx, [fm_cy]
    add dx, [fm_listb]
    dec dx
    mov [fm_sby2], dx           ; abs y2
    mov byte [gfx_color], CBLACK
    call gfx_frame

    mov ax, [fm_sbx1]           ; arrow-cell rules, 11 rows in from each end
    mov bx, [fm_rgt]
    mov dx, [fm_sby1]
    add dx, 10
    call gfx_hline
    mov dx, [fm_sby2]
    sub dx, 10
    call gfx_hline

    mov bp, 0                   ; up glyph: rows y1+3..y1+7, widths 1..9
    mov dx, [fm_sby1]
    add dx, 3
.uparr:
    mov ax, [fm_sbx1]
    add ax, 6
    sub ax, bp
    mov bx, [fm_sbx1]
    add bx, 6
    add bx, bp
    call gfx_hline
    inc dx
    inc bp
    cmp bp, 5
    jb .uparr

    mov bp, 4                   ; down glyph: rows y2-7..y2-3, widths 9..1
    mov dx, [fm_sby2]
    sub dx, 7
.dnarr:
    mov ax, [fm_sbx1]
    add ax, 6
    sub ax, bp
    mov bx, [fm_sbx1]
    add bx, 6
    add bx, bp
    call gfx_hline
    inc dx
    dec bp
    jns .dnarr

    mov ax, [fm_sbx1]           ; track: gray between the rules
    inc ax
    mov bx, [fm_sby1]
    add bx, 11
    mov cx, [fm_rgt]
    dec cx
    mov dx, [fm_sby2]
    sub dx, 11
    cmp bx, dx
    jg .nothumb                 ; degenerate track
    call gfx_fill_gray

    call fm_thumb               ; CF=1: everything fits, no thumb
    jc .nothumb
    mov bx, ax                  ; y1 = thumb top (abs)
    add dx, ax
    dec dx                      ; y2 = top + height - 1
    mov ax, [fm_sbx1]
    add ax, 2
    mov cx, [fm_rgt]
    sub cx, 2
    mov byte [gfx_color], CWHITE
    call gfx_fill
    mov byte [gfx_color], CBLACK
    call gfx_frame
.nothumb:

.norows:
    ; --- status line at (6, staty): the precedence is binding (SPEC.md 22),
    ;     because all four of these can be true at the same moment. The
    ;     header buffer is free by now, so every branch stages into it and
    ;     one truncation serves them all.
    mov di, fm_hdrbuf
    mov si, fm_s_loading        ; 1. a pending load trumps everything
    cmp word [ld_pending], 0
    jne .stat
    cmp byte [fm_edit], 0       ; 2. the folder name being typed, prompt and
    je .st_err                  ;    all - the editor IS the status line
    mov si, fm_s_newfld
    call fm_scat
    mov si, fm_ebuf
    jmp short .stat
.st_err:
    mov al, [fm_ferr]           ; 3. the last file operation's verdict, so
    mov bx, fm_errtab           ;    no dskw_* failure is ever silent
    or al, al
    jnz .st_tab
    mov al, [ld_status]         ; 4. and finally the loader's
    mov bx, fm_stattab
    cmp al, 5
    ja .statdone                ; out-of-range status: draw nothing
.st_tab:
    cmp al, FM_NERR
    jae .statdone
    xor ah, ah
    shl ax, 1
    add bx, ax
    mov si, [bx]
.stat:
    call fm_scat                ; stage it, then truncate to the live cw
    mov ax, [fm_cw]             ; ("Out of memory" is 104px and cw can
    sub ax, 12                  ; legally be 94, SPEC.md 22)
    jg .stpos
    xor ax, ax
.stpos:
    mov cl, 3
    shr ax, cl                  ; AX = chars that fit
    cmp ax, 25
    jae .stdraw
    mov bx, ax
    mov byte [fm_hdrbuf+bx], 0
.stdraw:
    cmp byte [fm_hdrbuf], 0
    je .statdone
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov dx, [fm_cy]
    add dx, [fm_staty]
    mov si, fm_hdrbuf
    call font_str
.statdone:

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_scat - append a NUL string to a buffer
; in:  SI = NUL source, DI = destination
; out: DI -> the stored NUL (so the next append overwrites it); SI advanced
; clobbers: AL (internal helper)
; -----------------------------------------------------------------------------
fm_scat:
    lodsb
    mov [di], al
    test al, al
    jz .done
    inc di
    jmp fm_scat
.done:
    ret

; -----------------------------------------------------------------------------
; fm_utoa - unsigned word to decimal ASCII, no leading zeros
; in:  AX = value, DI = destination (needs up to 6 bytes: 5 digits + NUL)
; out: DI -> the stored NUL
; clobbers: nothing else (internal helper)
; Digits are pushed least-significant first, then popped in print order.
; -----------------------------------------------------------------------------
fm_utoa:
    push ax
    push bx
    push cx
    push dx

    mov bx, 10
    xor cx, cx                  ; CX = digit count
.div:
    xor dx, dx
    div bx                      ; AX = quotient, DX = remainder
    add dl, '0'
    push dx
    inc cx
    test ax, ax
    jnz .div
.emit:
    pop ax
    mov [di], al
    inc di
    loop .emit
    mov byte [di], 0

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
section .bss

fm_sel:     resw 1              ; selected directory index, 0xFFFF = none
fm_clkt:    resw 1              ; click birth tick (EV_C) of the last click
fm_mountok: resb 1              ; non-zero when the last mount succeeded
fm_view:    resb 1              ; 0 = list view, 1 = icon view
fm_scroll:  resw 1              ; first visible row (entry row / grid row)
fm_ferr:    resb 1              ; FERR_* of the last file operation, 0 = none
fm_full:    resb 1              ; 1 = the next fm_repaint owes the frame too
fm_titlep:  resw 1              ; -> the instance's I_NAME, 0 = no window
fm_cmdwin:  resw 1              ; fm_oncmd scratch: the acting window ptr

fm_edit:    resb 1              ; status-line editor: 0 = off, 1 = new folder
fm_elen:    resb 1              ; characters typed so far (0..12)
fm_ebuf:    resb 13             ; the name being typed, NUL-terminated

fm_cx:      resw 1              ; fm_layout: content left (abs)
fm_cy:      resw 1              ; fm_layout: content top (abs)
fm_cw:      resw 1              ; fm_layout: content width
fm_ch:      resw 1              ; fm_layout: content height
fm_rgt:     resw 1              ; fm_layout: content right (abs, inclusive)
fm_staty:   resw 1              ; fm_layout: status text top (content-rel)
fm_listb:   resw 1              ; fm_layout: first row below the row area
fm_fit:     resw 1              ; fm_layout: whole rows that fit
fm_cols:    resw 1              ; fm_layout: grid columns (1 in list view)
fm_total:   resw 1              ; fm_layout: total rows
fm_nch:     resw 1              ; fm_layout: list-view name chars that fit

fm_shown:   resw 1              ; draw scratch: rows drawn this paint
fm_fsize:   resw 1              ; fm_stage_name: entry size, bytes
fm_ftype:   resw 1              ; fm_stage_name: SPEC.md 19 type word
fm_vrow:    resw 1              ; grid scratch: visible row / column
fm_vcol:    resw 1
fm_cellx:   resw 1              ; grid scratch: current cell origin (abs)
fm_celly:   resw 1
fm_sbx1:    resw 1              ; scroll bar scratch: left col (abs)
fm_sby1:    resw 1              ; top row (abs)
fm_sby2:    resw 1              ; bottom row (abs)

fm_name:    resb 17             ; NUL-safe copy of one 16-byte name field
fm_hdrbuf:  resb 26             ; header line ("Drive B:  65535 files" max 22)
fm_numbuf:  resb 6              ; fm_utoa scratch: 5 digits + NUL

section .text
