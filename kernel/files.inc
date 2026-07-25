; =============================================================================
; jop - files.inc
;
; The Disk window - a minimal file manager for the jopfs data floppy
; (SPEC.md 22). A singleton app kind (KIND_FILES, cap 1 - the mount state
; below is module-global): no boot-time window; app_launch creates it on
; demand (fm_kinit is the KD_INIT). CMD_FILES dispatch calls files_open and
; desk_click calls files_open_drive - both ALWAYS remount (that is the
; disk-swap story: a swapped disk must never show stale contents) and end
; in app_launch, which creates the window or fronts + un-minimizes the
; existing instance. Rows list the directory (icon +
; name + size); a double-click (same row twice within 9 ticks) posts the
; entry to [ld_pending] - ui.inc runs loader_run after the lock drops, and
; loader_run calls files_refresh with the result in [ld_status]. A Refresh
; button at the top right remounts the current drive in place. The
; paint/onkey/onclick procs run with the gfx lock already held by the
; caller (SPEC.md 11) and never lock, block or spawn; files_open /
; files_open_drive / files_refresh take the lock themselves.
; =============================================================================

; --- layout (content-relative, SPEC.md 22) -----------------------------------
FM_ROW_H    equ 16              ; file row height, px
FM_ROW_Y0   equ 22              ; first row top
FM_MAX_ROWS equ 8               ; rows shown (entries beyond are not listed);
                                ; 8 keeps the last row band (ends at y=149)
                                ; clear of the status line at y=164
FM_DBLCLK   equ 9               ; double-click window, ticks

; Refresh button rect, content-relative: content_w = W_W-2 = 318, so
; (content_w-68, 2) .. (content_w-6, 15) per SPEC.md 22.
FM_BTN_X1   equ 250
FM_BTN_Y1   equ 2
FM_BTN_X2   equ 312
FM_BTN_Y2   equ 15

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
    ret

; -----------------------------------------------------------------------------
; fm_kinit - KD_INIT for the Disk kind (SPEC.md 29.3): fresh instance state
; in:  BX = window ptr, DI = state ptr (0 - stateless), SI = instance record
; out: nothing (all registers preserved)
; -----------------------------------------------------------------------------
fm_kinit:
    mov word [fm_sel], 0xFFFF   ; a fresh window starts with no selection
    mov word [fm_clkt], 0
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
fm_tpl:     dw 110, 80, 320, 200, fm_ttl, fm_paint, fm_onkey, fm_onclick
fm_ttl:     db 'Disk', 0

fm_s_drive:  db 'Drive ', 0     ; header: "Drive " letter ":  " N " files"
fm_s_colsp:  db ':  ', 0
fm_s_files:  db ' files', 0
fm_s_nodisk: db 'No jop disk in drive ', 0
fm_s_refresh: db 'Refresh', 0   ; button label, 7 chars = 56 px

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
; selection is cleared. disk_mount runs OUTSIDE the gfx lock (SPEC.md 18).
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
; The Refresh button rect is tested FIRST (SPEC.md 22): inside it, remount
; the current drive, update fm_mountok, clear the selection, repaint. Else
; map the click to a row (guarding the header band y < 22 and rows past the
; listing); same row twice within FM_DBLCLK ticks = double-click, which posts
; [ld_pending] = row + 1 for ui.inc to run after the lock drops. Every path
; repaints own content ("Loading..." shows immediately on a double-click).
; -----------------------------------------------------------------------------
fm_onclick:
    push ax
    push bx
    push cx
    push dx

    mov bx, si
    push cx                     ; save click x
    mov cx, dx                  ; CX = click y
    call wm_content             ; AX = content left, DX = content top
    sub cx, dx                  ; CX = content-relative y
    pop dx
    sub dx, ax                  ; DX = content-relative x

    ; --- Refresh button first (SPEC.md 22) ---
    cmp dx, FM_BTN_X1
    jl .rows
    cmp dx, FM_BTN_X2
    jg .rows
    cmp cx, FM_BTN_Y1
    jl .rows
    cmp cx, FM_BTN_Y2
    jg .rows
    mov dl, [disk_drive]        ; remount the current drive in place
    call disk_mount             ; (under the gfx lock: stalls painters
    mov byte [fm_mountok], 0    ;  briefly; accepted by SPEC.md 22)
    jc .clear                   ; either way: clear selection + repaint
    mov byte [fm_mountok], 1
    jmp .clear

.rows:
    sub cx, FM_ROW_Y0           ; content-relative y, rebased to row 0
    js .clear                   ; header band: y < 22 clears the selection
    mov ax, cx
    mov cx, FM_ROW_H
    xor dx, dx
    div cx                      ; AX = row index
    cmp ax, FM_MAX_ROWS
    jae .clear                  ; below the listed rows
    cmp ax, [disk_nfiles]
    jae .clear                  ; past the directory

    cmp ax, [fm_sel]            ; second click on the selected row?
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
; SPEC.md 22). Enter opens the selection like a double-click.
; -----------------------------------------------------------------------------
fm_onkey:
    push ax
    push bx
    push dx

    cmp al, 13
    je .enter
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
    jne .out
.remount:
    mov dl, [disk_drive]

.mount:
    call disk_mount             ; sets [disk_drive]; CF = failure
    mov byte [fm_mountok], 0
    jc .mounted
    mov byte [fm_mountok], 1
.mounted:
    mov word [fm_sel], 0xFFFF   ; directory changed: selection is stale
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
; from wm_paint_all, but self-initiated repaints must fill it themselves.
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

    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; fm_draw_core - draw the whole content (header, button, rows, selection,
; status)
; in:  BX = window ptr (content already white, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Header at (6,6): "Drive X:  N files", or "No jop disk in drive X:" when
; the last mount failed. Refresh button: 1px frame at FM_BTN_* with the
; label centered inside. Rows are FM_ROW_H tall from FM_ROW_Y0: the file's
; 16x16 icon at x=4, row top (disk_icons entry i, body-only layout via
; icon_draw16; an all-zero entry falls back to the built-in ico_app16
; library record via icon_draw - SPEC.md 19/25), name at x=24 (copied to a
; local buffer to guarantee NUL termination - directory names are
; NUL-PADDED 16-byte fields), size right-aligned at content right - 6,
; text at row top + 4. The selected row is inverted with gfx_xor_fill
; after drawing. Status line at content-relative (6, 182-TITLE_H) from
; [ld_status], or "Loading..." while [ld_pending] is armed.
; -----------------------------------------------------------------------------
fm_draw_core:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call wm_content             ; AX = content left, DX = content top
    mov [fm_cx], ax
    mov [fm_cy], dx
    mov ax, [bx+W_X]
    add ax, [bx+W_W]
    sub ax, 2
    mov [fm_rgt], ax            ; content right, inclusive

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
    jmp .drawhdr
.badhdr:
    mov si, fm_s_nodisk         ; "No jop disk in drive X:"
    call fm_scat
    mov al, [disk_drive]
    add al, 'A'
    mov [di], al
    inc di
    mov byte [di], ':'
    inc di
    mov byte [di], 0
.drawhdr:
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov dx, [fm_cy]
    add dx, 6
    mov si, fm_hdrbuf
    call font_str

    ; --- Refresh button: 1px frame + centered label (SPEC.md 22) ---
    mov ax, [fm_cx]
    add ax, FM_BTN_X1
    mov bx, [fm_cy]
    add bx, FM_BTN_Y1
    mov cx, [fm_cx]
    add cx, FM_BTN_X2
    mov dx, [fm_cy]
    add dx, FM_BTN_Y2
    call gfx_frame              ; color still CBLACK from the header
    mov cx, [fm_cx]
    add cx, FM_BTN_X1 + 3       ; 56px label centered in the 63px rect
    mov dx, [fm_cy]
    add dx, FM_BTN_Y1 + 2
    mov si, fm_s_refresh
    call font_str

    ; --- file rows ---
    mov ax, [disk_nfiles]       ; rows shown = min(nfiles, FM_MAX_ROWS);
    cmp ax, FM_MAX_ROWS         ; 0 after a failed mount, so the loop and
    jbe .cnt                    ; the selection bar naturally vanish
    mov ax, FM_MAX_ROWS
.cnt:
    mov [fm_cnt], ax

    xor di, di                  ; DI = row index
.rowloop:
    cmp di, [fm_cnt]
    jae .rowsdone
    mov ax, FM_ROW_H            ; BP = row band top (absolute)
    mul di
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bp, ax

    ; --- 16x16 icon at x = 4, row top ---
    mov ax, 64                  ; SI = disk_icons + row*64 (SPEC.md 19)
    mul di
    add ax, disk_icons
    mov si, ax
    mov bx, si                  ; all-zero entry = no icon: OR the 32
    xor ax, ax                  ; words together to find out
    mov cx, 32
.orwords:
    or ax, [bx]
    inc bx
    inc bx
    loop .orwords
    mov cx, [fm_cx]
    add cx, 4
    mov dx, bp
    test ax, ax
    jz .noicon
    call icon_draw16            ; body-only table entry (SPEC.md 25)
    jmp .icondone
.noicon:
    mov si, ico_app16           ; fall back to the built-in app icon
    call icon_draw              ; (library record, with header)
.icondone:

    mov ax, 32                  ; SI = disk_dir + row*32 (SPEC.md 19)
    mul di
    add ax, disk_dir
    mov si, ax
    add bp, 4                   ; BP = text y: row top + 4 centers the
                                ; 8px glyphs in the 16px band

    mov bx, fm_name             ; name field is NUL-padded, not guaranteed
    mov cx, 16                  ; NUL-terminated: copy all 16 bytes and
.cpname:                        ; force a terminator
    lodsb                       ; DF=0 on entry per SPEC.md 1
    mov [bx], al
    inc bx
    loop .cpname
    mov byte [fm_name+16], 0

    push si                     ; SI -> entry+16 (type; size is at +4)
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 24
    mov dx, bp
    mov si, fm_name
    call font_str               ; name at x = 24
    pop si

    mov ax, [si+4]              ; size in bytes (entry offset 20)
    push di
    mov di, fm_numbuf
    call fm_utoa
    pop di
    push si
    mov si, fm_numbuf
    call font_width             ; AX = string width, px
    mov cx, [fm_rgt]            ; right-align: last pixel column lands on
    sub cx, 5                   ; content right - 6
    sub cx, ax
    mov dx, bp
    call font_str
    pop si

    inc di
    jmp .rowloop

.rowsdone:
    ; --- selection bar (inverted row) ---
    mov ax, [fm_sel]
    cmp ax, [fm_cnt]            ; 0xFFFF (none) also fails this
    jae .nosel
    mov cx, FM_ROW_H
    mul cx                      ; AX = sel * 16
    add ax, [fm_cy]
    add ax, FM_ROW_Y0
    mov bx, ax                  ; y1 = row band top
    mov dx, ax
    add dx, FM_ROW_H - 1        ; y2
    mov ax, [fm_cx]             ; full content width
    mov cx, [fm_rgt]
    call gfx_xor_fill
.nosel:

    ; --- status line ---
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
    mov byte [gfx_color], CBLACK
    mov cx, [fm_cx]
    add cx, 6
    mov dx, [fm_cy]
    add dx, 182 - TITLE_H       ; content-relative y (SPEC.md 22)
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

fm_sel:     resw 1              ; selected row, 0xFFFF = none
fm_clkt:    resw 1              ; click birth tick (EV_C) of the last row click
fm_mountok: resb 1              ; non-zero when the last mount succeeded

fm_cx:      resw 1              ; draw scratch: content left
fm_cy:      resw 1              ; draw scratch: content top
fm_rgt:     resw 1              ; draw scratch: content right, inclusive
fm_cnt:     resw 1              ; draw scratch: rows shown

fm_name:    resb 17             ; NUL-safe copy of one 16-byte name field
fm_hdrbuf:  resb 26             ; header line ("Drive B:  65535 files" max 22)
fm_numbuf:  resb 6              ; fm_utoa scratch: 5 digits + NUL

section .text
