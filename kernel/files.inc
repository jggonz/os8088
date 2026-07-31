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
; A double-click on an entry (same directory index twice within 9 ticks)
; posts index+1 to [ld_pending] - ui.inc runs loader_run after the lock
; drops, and loader_run calls files_refresh with the result in
; [ld_status]. The paint/onkey/onclick procs run with the gfx lock already
; held by the caller (SPEC.md 11) and never lock, block or spawn;
; files_open / files_open_drive / files_refresh take the lock themselves.
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
    ret

; -----------------------------------------------------------------------------
; fm_kinit - KD_INIT for the Disk kind (SPEC.md 29.3): fresh instance state
; in:  BX = window ptr, DI = state ptr (0 - stateless), SI = instance record
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fm_kinit:
    mov word [fm_sel], 0xFFFF   ; a fresh window starts with no selection
    mov word [fm_clkt], 0
    mov byte [fm_view], 0       ; list view, scrolled to the top
    mov word [fm_scroll], 0
    mov word [bx + W_MENUS], menu_loc_set
                                ; the Disk browser IS Locator's window
                                ; (SPEC.md 12.3): fronting it keeps Locator
                                ; and its menus in the bar rather than
                                ; swapping them for an app named 'Disk'
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
; -----------------------------------------------------------------------------
fm_onclick:
    push ax
    push bx
    push cx
    push dx
    push di

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
    call disk_mount             ; (under the gfx lock: stalls painters
    mov byte [fm_mountok], 0    ;  briefly; accepted by SPEC.md 22)
    jc .refresh_done
    mov byte [fm_mountok], 1
.refresh_done:
    mov word [fm_scroll], 0     ; either way: top, no selection, repaint
    jmp .clear

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
    inc ax                      ; double-click: post directory index + 1
    mov [ld_pending], ax
    jmp .redraw

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
; 'a'/'A' mounts drive 0, 'b'/'B' drive 1, 'r'/'R' remounts the current
; drive (disk_mount under the gfx lock stalls painters briefly; accepted by
; SPEC.md 22). 'v'/'V' toggles the view. Up/Down/PgUp/PgDn (scan 48h/50h/
; 49h/51h) scroll the view. Enter opens the selection like a double-click.
; -----------------------------------------------------------------------------
fm_onkey:
    push ax
    push bx
    push dx

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
.remount:
    mov dl, [disk_drive]

.mount:
    call disk_mount             ; sets [disk_drive]; CF = failure
    mov byte [fm_mountok], 0
    jc .mounted
    mov byte [fm_mountok], 1
.mounted:
    mov word [fm_sel], 0xFFFF   ; directory changed: selection and scroll
    mov word [fm_scroll], 0     ; are stale
    jmp .redraw

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
    inc ax                      ; same as a double-click
    mov [ld_pending], ax

.redraw:
    call fm_repaint             ; SI still = window ptr

.out:
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_repaint - white-fill own content, then redraw everything
; in:  SI = window ptr (gfx lock held by caller)
; out: nothing
; clobbers: nothing (flags only)
; The Note Pad onkey idiom (SPEC.md 22): W_PAINT gets a pre-filled content
; from wm_paint_all, but self-initiated repaints must fill it themselves -
; from the LIVE record: the window resizes (SPEC.md 11.1).
; -----------------------------------------------------------------------------
fm_repaint:
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
    call gfx_fill               ; white-fill content

    mov bx, si
    call fm_draw_core
    call wm_grow_paint          ; the white fill above erased the grow box;
                                ; BX is still the window ptr (SPEC.md 11.1)

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
; fm_stage_name - stage a directory entry's name and size
; in:  AX = directory index
; out: fm_name = NUL-terminated copy of the 16-byte name field (which is
;      NUL-PADDED, not NUL-terminated - the copy forces a terminator),
;      [fm_fsize] = size in bytes. All registers preserved.
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
    mov ax, [si+4]              ; size word (entry offset 20; SI advanced
    mov [fm_fsize], ax          ; past the name)
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
    ; --- status line at (6, staty) ---
    mov si, fm_s_loading        ; a pending load trumps the last status
    cmp word [ld_pending], 0
    jne .stat
    mov al, [ld_status]
    cmp al, 5
    ja .statdone                ; out-of-range status: draw nothing
    xor ah, ah
    mov bx, ax
    shl bx, 1
    mov si, [fm_stattab+bx]
.stat:
    cmp byte [si], 0            ; status 0 = empty string, skip the call
    je .statdone
    mov di, fm_hdrbuf           ; the header buffer is free by now: stage
    call fm_scat                ; the string and truncate it to the live
    mov ax, [fm_cw]             ; cw ("Out of memory" is 104px and cw can
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
