; =============================================================================
; jop - files.inc
;
; The Disk window - a minimal file manager for the jopfs data floppy
; (SPEC.md 22). files_init creates the window hidden at boot; CMD_FILES
; dispatch calls files_open, which mounts the disk on first open and shows
; the window. Rows list the directory (name + size); a double-click (same
; row twice within 9 ticks) posts the entry to [ld_pending] - ui.inc runs
; loader_run after the lock drops, and loader_run calls files_refresh with
; the result in [ld_status]. The paint/onkey/onclick procs run with the gfx
; lock already held by the caller (SPEC.md 11) and never lock, block or
; spawn; files_open / files_refresh take the lock themselves.
; =============================================================================

; --- layout (content-relative, SPEC.md 22) -----------------------------------
FM_ROW_H    equ 12              ; file row height, px
FM_ROW_Y0   equ 22              ; first row top
FM_MAX_ROWS equ 11              ; rows shown (entries beyond are not listed);
                                ; 11 keeps the last row band (ends at y=153)
                                ; clear of the status line at y=164
FM_DBLCLK   equ 9               ; double-click window, ticks

; -----------------------------------------------------------------------------
; files_init - create the Disk window (hidden) and reset module state
; in:  nothing
; out: nothing; window ptr stored in fm_win
; clobbers: nothing (flags only)
; .bss is not zeroed by the loader: every state variable is set here.
; -----------------------------------------------------------------------------
files_init:
    push bx
    push si

    mov word [fm_sel], 0xFFFF   ; no selection
    mov word [fm_clkt], 0
    mov byte [fm_mounted], 0    ; mount happens on first files_open
    mov byte [fm_mountok], 0

    mov si, fm_tpl
    call wm_create
    mov [fm_win], bx            ; stays hidden until CMD_FILES

    pop si
    pop bx
    ret

; --- window template: {x, y, w, h, title, paint, onkey, onclick} words -------
fm_tpl:     dw 110, 80, 320, 200, fm_ttl, fm_paint, fm_onkey, fm_onclick
fm_ttl:     db 'Disk', 0

fm_s_drive:  db 'Drive ', 0     ; header: "Drive " letter ":  " N " files"
fm_s_colsp:  db ':  ', 0
fm_s_files:  db ' files', 0
fm_s_nodisk: db 'No jop disk in drive ', 0

fm_s_loading: db 'Loading...', 0
fm_s_empty:   db 0              ; status line strings, indexed by [ld_status]
fm_s_dskerr:  db 'Disk error', 0
fm_s_badpkg:  db 'Bad package', 0
fm_s_toobig:  db 'Too large', 0
fm_s_ldfail:  db 'Load failed', 0
fm_stattab:   dw fm_s_empty, fm_s_dskerr, fm_s_badpkg, fm_s_toobig, fm_s_ldfail

; -----------------------------------------------------------------------------
; files_open - CMD_FILES dispatch entry (ui.inc); no lock held on entry
; in:  nothing
; out: nothing
; clobbers: nothing (flags only)
; First open mounts drive [disk_drive] (initial B:); the mounted flag is set
; even on failure - the window then shows the failure line and 'r' retries
; (SPEC.md 22). disk_mount is called OUTSIDE the gfx lock (SPEC.md 18).
; -----------------------------------------------------------------------------
files_open:
    push bx
    push dx

    cmp byte [fm_mounted], 0
    jne .show
    mov dl, [disk_drive]
    call disk_mount
    mov byte [fm_mountok], 0
    jc .fail
    mov byte [fm_mountok], 1
.fail:
    mov byte [fm_mounted], 1    ; even on failure (SPEC.md 22)

.show:
    call gfx_lock
    mov bx, [fm_win]
    call wm_show                ; visible + front + repaint all
    call gfx_unlock

    pop dx
    pop bx
    ret

; -----------------------------------------------------------------------------
; files_refresh - repaint after a load attempt (loader_run)
; in:  nothing; no lock held on entry
; out: nothing
; clobbers: nothing (flags only)
; Takes the gfx lock itself; hidden window = nothing to do. This must be a
; FULL wm_paint_all, not a content-only repaint: loader_run calls it right
; after wm_show raised the loaded program's window, which may overlap the
; Disk window - painting only our content would draw over the new front
; window. Rare (once per load attempt), so the full repaint is cheap enough.
; -----------------------------------------------------------------------------
files_refresh:
    push si

    call gfx_lock
    mov si, [fm_win]
    test word [si+W_FLAGS], 2   ; visible?
    jz .skip
    call wm_paint_all
.skip:
    call gfx_unlock

    pop si
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
; Maps the click to a row (guarding the header band y < 22 and rows past the
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
    mov cx, dx                  ; CX = click y (x is not needed)
    call wm_content             ; AX = content left, DX = content top
    sub cx, dx
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
    mov dx, [ticks]
    sub dx, [fm_clkt]           ; wrap-safe (SPEC.md 8)
    cmp dx, FM_DBLCLK
    jae .select
    inc ax                      ; double-click: post directory index + 1
    mov [ld_pending], ax
    jmp .redraw

.select:
    mov [fm_sel], ax
    mov dx, [ticks]
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
    mov byte [fm_mounted], 1
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
; fm_draw_core - draw the whole content (header, rows, selection, status)
; in:  BX = window ptr (content already white, gfx lock held)
; out: nothing
; clobbers: nothing (flags only)
; Header at (6,6): "Drive X:  N files", or "No jop disk in drive X:" when
; the last mount failed. Rows are FM_ROW_H tall from FM_ROW_Y0, name at x=6
; (copied to a local buffer to guarantee NUL termination - directory names
; are NUL-PADDED 16-byte fields), size right-aligned at content right - 6.
; The selected row is inverted with gfx_xor_fill after drawing. Status line
; at content-relative (6, 182-TITLE_H) from [ld_status], or "Loading..."
; while [ld_pending] is armed.
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
    mov ax, 32                  ; SI = disk_dir + row*32 (SPEC.md 19)
    mul di
    add ax, disk_dir
    mov si, ax
    mov ax, FM_ROW_H            ; BP = text y: row top + 2 centers the
    mul di                      ; 8px glyphs in the 12px band
    add ax, [fm_cy]
    add ax, FM_ROW_Y0 + 2
    mov bp, ax

    mov bx, fm_name             ; name field is NUL-padded, not guaranteed
    mov cx, 16                  ; NUL-terminated: copy all 16 bytes and
.cpname:                        ; force a terminator
    lodsb                       ; DF=0 on entry per SPEC.md 1
    mov [bx], al
    inc bx
    loop .cpname
    mov byte [fm_name+16], 0

    push si                     ; SI -> entry+16 (type; size is at +4)
    mov cx, [fm_cx]
    add cx, 6
    mov dx, bp
    mov si, fm_name
    call font_str               ; name at x = 6
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
    mul cx                      ; AX = sel * 12
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
    cmp al, 4
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

fm_win:     resw 1              ; Disk window record ptr
fm_sel:     resw 1              ; selected row, 0xFFFF = none
fm_clkt:    resw 1              ; [ticks] at the last row click
fm_mounted: resb 1              ; non-zero once files_open has mounted
fm_mountok: resb 1              ; non-zero when the last mount succeeded

fm_cx:      resw 1              ; draw scratch: content left
fm_cy:      resw 1              ; draw scratch: content top
fm_rgt:     resw 1              ; draw scratch: content right, inclusive
fm_cnt:     resw 1              ; draw scratch: rows shown

fm_name:    resb 17             ; NUL-safe copy of one 16-byte name field
fm_hdrbuf:  resb 26             ; header line ("Drive B:  65535 files" max 22)
fm_numbuf:  resb 6              ; fm_utoa scratch: 5 digits + NUL

section .text
