; =============================================================================
; os8088 - apps/chart/chart.asm
;
; CHART, a standalone bar-chart viewer: File > Open... reads a real SYLK,
; DIF or BIFF file (dispatched by its extension, exactly like Sheet's own
; sh_doread) and renders the FIRST NUMERIC COLUMN it finds as a bar chart;
; File > Export as BMP... saves the rendered chart as a real graphics file
; for use in other software. Launch is via the standard Open dialog, not
; double-click file association - there is no cross-app spawn API anywhere
; in this OS (confirmed by an exhaustive apps/os88api.inc search), so
; association would only add complexity to a launch path that still
; requires going through the Locator either way.
;
; Deliberately does NOT reuse Sheet's own SYLK/DIF/BIFF reader code
; (apps/sheet/sheet.asm's sh_doread_sylk/sh_doread_dif/sh_doread_biff):
; this app reads files it did NOT write, so a reader that (like Sheet's
; own DIF reader) assumes its own writer's exact fixed shape would be
; unsafe here. The one exception is ct_rkdec below, a verbatim duplicate
; of sheet.asm's sh_rkdec - a tiny, fully self-contained 4-byte-value
; decode with no dependency on anything else in that file, so duplicating
; it exactly is cheap and safe where reusing a whole reader would not be.
;
; Rendering and BMP export are shared with Sheet's own live "Data > Chart
; Column..." window via apps/os88chart.inc (ch_bars_draw/ch_bmp_write) -
; see that file's own header for the offscreen-buffer design this is
; built on (there is no pixel-readback API in this OS, so both the
; on-screen chart and the exported file come from one rasterized buffer).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'CHART', ct_entry

; --- shared chart geometry constants (see apps/os88chart.inc's own header -
; equ constants can't be forward-referenced, and that file's CODE has to
; live at the end of this package, so these are duplicated here exactly as
; apps/sheet/sheet.asm's own copy is - the reference for every field) -------
CH_W       equ 240
CH_H       equ 160
CH_STRIDE  equ 120                  ; CH_W / 2 (4bpp, 2px/byte)
CH_HDRSZ   equ 118                  ; 54-byte BMP header + 64-byte palette
CH_PXOFF   equ CH_HDRSZ             ; pixel data starts right after
CT_NTXT_MAX equ 24                  ; ct_esatof: the longest number text
                                    ; it will copy out of a staged file
CH_MAXBARS equ 40                   ; how many values the caller's arrays
                                     ; hold - NOT a drawing limit: ch_band
                                     ; divides the axis among however many
                                     ; there are, so any count up to this one
                                     ; fits the canvas
CH_T_COLUMN equ 0                   ; stage 3.0f: the gallery. Excel calls the
CH_T_BAR    equ 1                   ; vertical one Column and the horizontal
CH_T_LINE   equ 2                   ; one Bar, and this follows that naming
CH_T_AREA   equ 3                   ; rather than the intuitive-but-wrong one
CH_T_PIE    equ 4                   ; stage 3.0f, and the last of the four
CH_T_SCATTER equ 5                  ; ...and stage 3.0f's own last two, which
CH_T_COMBO   equ 6                  ; needed a SECOND series (SPEC.md 82.8)
                                    ; Excel types this app can draw: Scatter
                                    ; and Combination need TWO series, which
                                    ; is a data-model problem rather than a
                                    ; drawing one

CT_CLAIM_CHART_KB equ 19            ; the offscreen 4bpp canvas (19200 bytes
                                     ; needed -> 19KB claimed, 256B slack)
CT_CLAIM_STG_KB   equ 32            ; file-read staging AND BMP-export
                                     ; staging - sequential uses, never
                                     ; concurrent, the same reuse Sheet's own
                                     ; sh_stgseg already makes between its
                                     ; file I/O and (via sh_docmd_chartexport)
                                     ; its own chart export
CT_NAMEMAX equ 12                   ; 8.3 name, no NUL
CT_WIN_W   equ 260                  ; a little margin around the CH_W x
CT_WIN_H   equ 200                  ; CH_H canvas
; The temp arrays hold the KEPT SERIES, not the scanned candidates - see
; ct_record for why that distinction was a silent data-loss bug. They are
; therefore sized by CH_MAXBARS, the most that can ever be drawn, rather than
; by a separate and much larger scan cap (CT_TCAP, 256, now retired: it cost
; ~1.3KB of bss to hold cells that were going to be discarded anyway).

FDLG_OPEN equ 0
FDLG_SAVE equ 1

; -----------------------------------------------------------------------------
; ct_entry - package entry point (SPEC.md 20.2). Claims run here (the one
; place a package has no window yet), the constant BMP header+palette are
; copied into the chart buffer once (see os88chart.inc's own ch_hdrtpl
; comment: "copy this once ... ch_bmp_write just stages whatever is
; already sitting there"), then the window and its File menu are created.
; -----------------------------------------------------------------------------
ct_entry:
    push ax
    push cx
    push dx
    push si
    push di
    push es
    call fp_init                        ; stage 4.6: before the first claim,
                                        ; for the reason sheet.asm's own call
                                        ; states - it decides which arithmetic
                                        ; the session gets, and nothing else
                                        ; here can recover from it being wrong
    mov ax, CT_CLAIM_CHART_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [ct_chartseg], dx
    mov ax, CT_CLAIM_STG_KB
    call OSAPI_MEM_CLAIM
    jc .fail
    mov [ct_stgseg], dx
    mov word [ct_valcnt], 0
    mov byte [ct_name], 0
    mov es, [ct_chartseg]               ; copy the constant 118-byte BMP
    mov si, ch_hdrtpl                   ; header+palette into the buffer
    xor di, di                          ; once, here - ch_bmp_write only
    mov cx, CH_HDRSZ                    ; ever stages whatever's already
    cld                                 ; sitting there, never rebuilds it
    rep movsb
    mov si, ct_tpl
    call OSAPI_WM_CREATE                ; BX = window ptr, CF on table full
    jc .fail
    mov si, ct_menus
    call OSAPI_MENU_SET                 ; preserves CF (SPEC.md 20.3)
    mov si, ct_about                    ; ...and 'About Chart' above its Close
    call OSAPI_ABOUT_SET                ; (SPEC.md 12.2), which every other
                                         ; package in the tree declares and
                                         ; this one did not
    jmp .out
.fail:
    stc
.out:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_paint - W_PAINT: the two bands the picture does not cover, then one
; OSAPI_GFX_BLIT4 of the already-rasterized buffer.
; In: SI = window ptr; caller holds the gfx lock.
; -----------------------------------------------------------------------------
ct_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov di, si                          ; the window, banked for the card:
    mov bx, si                          ; SI is spent on the blit below
    call OSAPI_WM_CONTENT               ; ax=content x, dx=content y
    call ch_margin                      ; THE INTERIOR THE PICTURE DOES NOT
                                         ; COVER (SPEC.md 82.1.1, issue #142) -
                                         ; CT_WIN_W x CT_WIN_H is the same
                                         ; 260x200 around the same CH_W x CH_H
                                         ; that SHEET's chart window is, so it
                                         ; had the same unwritten bands for the
                                         ; same reason. BX is still the window
                                         ; and AX/DX still WM_CONTENT's answer
    mov bx, dx                          ; bx=y for BLIT4 below
    mov es, [ct_chartseg]
    mov si, CH_PXOFF
    mov bp, CH_STRIDE
    mov cx, CH_W
    mov dx, CH_H
    call OSAPI_GFX_BLIT4
    pop es
    cmp byte [ct_abon], 0               ; ...and the About card LAST, over the
    je .noab                            ; canvas it is opaque about (20.5.1)
    push si
    mov bx, di                          ; the WINDOW - not SI, which is CH_PXOFF
    mov si, ct_ablines                  ; since the blit
    call os88ui_about_d                 ; _d: this paint's region is armed
    pop si
.noab:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_render - rasterize ct_vals[0..ct_valcnt) into ct_chartseg via
; apps/os88chart.inc's ch_bars_draw. The value array lives in THIS
; PACKAGE's own bss - a single-segment package (SPEC.md 20.1) runs with
; DS already pointed at that segment, so ch_bars_draw's own DX=array
; segment parameter is just DS itself, no cross-segment juggling needed
; (unlike Sheet, which stages the array in a separately claimed segment).
; -----------------------------------------------------------------------------
ct_render:
    push ax
    push bx                             ; ch_draw's own header says it clobbers
    push cx                             ; ax-dx, si and di. BX was NOT banked
    push dx                             ; here, and ct_ondlg keeps the WINDOW
    push si                             ; POINTER in it across this call - so
    push di                             ; `mov si, bx` fed ct_paint a garbage
    push es                             ; window and OSAPI_GFX_BLIT4 wrote a
                                        ; 240x160 image through whatever
                                        ; coordinates that address happened to
                                        ; hold: the menu bar and the window's
                                        ; own frame, destroyed, with no error.
                                        ; DI is banked for the same reason
                                        ; before it costs someone else a day.
    mov word [ch_arr2], ct_w2vals       ; the second series, if the file had a
    mov ax, [ct_t2cnt]                  ; second column (82.8)
    mov [ch_cnt2], ax
    mov ax, ds
    mov [ch_srcseg2], ax
    mov word [ch_title], ct_name        ; the file it charted, which is the
    cmp byte [ct_name], 0               ; only name this app has for the data
    jne .titled
    mov word [ch_title], 0
.titled:
    mov cx, [ct_valcnt]
    mov es, [ct_chartseg]
    mov dx, ds
    mov si, ct_wvals                    ; the SCALED words, not the doubles
    call ch_draw                        ; stage 3.0f: the type comes from
                                        ; [ch_type], which the Gallery menu
                                        ; sets; ch_draw falls back to the
                                        ; column chart for an unknown one
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_oncmd - the File menu (AL = item index: 0 Open..., 1 Export as
; BMP...); SI = the owning window, gfx lock already held (SPEC.md 12.2)
; -----------------------------------------------------------------------------
ct_oncmd:
    cmp ah, 1                           ; AH = the menu, AL = the item
    je .gallery
    cmp ah, 2
    je .data
    or al, al
    jnz .export
    push bx
    push si
    push di
    mov bx, si
    mov di, ct_ondlg
    xor si, si                          ; no default name for Open
    mov al, FDLG_OPEN
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret
.export:
    cmp word [ct_valcnt], 0
    jne .havedata
    push si
    mov si, ct_s_noexp
    call ct_toast
    pop si
    ret
.havedata:
    push bx
    push si
    push di
    mov bx, si
    mov di, ct_expdlg
    mov si, ct_s_chartbmp
    mov al, FDLG_SAVE
    call OSAPI_FILE_DLG
    pop di
    pop si
    pop bx
    ret
; --- Data: pick the column, and READ THE FILE AGAIN ---------------------------
; Re-reading rather than re-filtering what is in memory, because the readers
; keep only the two columns they chose - the rest was never stored. The file
; is already on the disk this instance came from, so this is one more read of
; a file that was just read.
.data:
    xor ah, ah
    mov [ct_wantcol], ax                ; 0 = Automatic, else the 1-based column
    call ct_reread
    ret

; --- Gallery: pick a type and redraw what is already loaded -------------------
; The item index maps to CH_T_* through this table rather than by arithmetic,
; because the menu is in Excel's alphabetical order (Area, Bar, Column, Line)
; and CH_T_* is in the order the drawing code was written.
.gallery:
    push bx
    push si
    xor bh, bh
    mov bl, al
    shl bl, 1
    mov ax, [ct_gal_map + bx]
    mov [ch_type], ax
    cmp word [ct_valcnt], 0
    je .galout                          ; nothing loaded: the type is still
    call ct_render                      ; remembered for the next Open
    call ct_paint
.galout:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; ct_about - the OSAPI_ABOUT_SET handler (slot 0x01E0, SPEC.md 12.2). SI = our
; window on entry; the UI task, gfx lock held.
;
; IT WAS A TOAST, on the argument that "an About is one line here" and that a
; card would cost the package os88ui.inc. Both halves were wrong: an About is
; one line only while it credits nobody, and SPEC.md 59's toast is a THREE
; SECOND TRANSIENT - it says what the package is to whoever is looking at that
; moment and then it is gone, which is not what a user picking About is asking
; for. This package and SHEET shipped with no attribution in them (SPEC.md
; 20.5.1), and this is the sentence that let it happen.
; -----------------------------------------------------------------------------
ct_about:
    push bx
    push si
    mov byte [ct_abon], 1
    mov bx, si
    mov si, ct_ablines
    call os88ui_about                   ; arms the clip itself: a menu dispatch
    pop si                              ; arrives without one (SPEC.md 11.3)
    pop bx
    ret

; -----------------------------------------------------------------------------
; ct_abdismiss - take the card down if it is up
; in:  SI = our window ptr; gfx lock held
; out: CF = 1 the click was spent doing it; preserves every register
;
; ct_paint is one OSAPI_GFX_BLIT4 of the whole canvas, so putting the content
; back is the paint itself - there is nothing incremental here to repair.
; -----------------------------------------------------------------------------
ct_abdismiss:
    cmp byte [ct_abon], 0
    je .none
    push bx
    mov byte [ct_abon], 0
    mov bx, si
    call OSAPI_WM_CLIP_SET              ; nothing has armed a region for a
    jc .gone                            ; click (SPEC.md 11.3)
    call ct_paint
.gone:
    pop bx
    stc
    ret
.none:
    clc
    ret

; -----------------------------------------------------------------------------
; ct_onclick - W_ONCLICK, and it exists for ONE reason: a card the user cannot
;              click away is not a card. This window is a pure display
;              otherwise and the handler does nothing else.
; in:  CX = x, DX = y, SI = window ptr; gfx lock held
; -----------------------------------------------------------------------------
ct_onclick:
    call ct_abdismiss
    ret

; ct_toast - in: SI = NUL message; shows it as a menu-bar toast for the
; default ~3s (SPEC.md 59). Preserves all registers except flags.
; -----------------------------------------------------------------------------
ct_toast:
    push ax
    push bx                             ; its header says "preserves all
    push cx                             ; registers", and it banked only AX,
    push dx                             ; CX and ES - a contract that was not
    push si                             ; true. Nothing relies on it today,
    push di                             ; but ct_render's missing `push bx`
    push es                             ; cost a corrupted menu bar and a
    push ds                             ; destroyed window frame (82.10), and
    pop es                              ; the kernel COPIES it (SPEC.md 59.3)
    xor cx, cx                          ; that started as a contract someone
    call OSAPI_TOAST                    ; read and believed
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_ondlg - the Open dialog's completion proc (SPEC.md 38.6). In: AL=mode
; (always 0, Open), SI=our window ptr, DI=chosen name (ES=KERNEL_SEG); UI
; task, gfx lock HELD, dialog already destroyed - we owe the repaint.
; Dispatches by extension into one of the three independent readers, then
; renders and blits whatever was found (zero values renders an empty white
; canvas, same as Sheet's own chart window with nothing charted yet).
; -----------------------------------------------------------------------------
ct_ondlg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov bx, si                          ; bx = our window ptr, stashed
    mov si, di
    mov di, ct_name
    mov cx, CT_NAMEMAX                  ; the count lives in CX - the loop
.copy:                                  ; body writes AL, so AX cannot hold it
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .copied
    inc si
    inc di
    dec cx
    jnz .copy
    mov byte [di], 0
.copied:
    mov word [ct_wantcol], 0            ; a newly opened file starts on
    call ct_read_by_ext                 ; Automatic, whatever the last one used
    jc .rerr
    call ct_render
    mov si, bx
    call ct_paint
    cmp word [ct_valcnt], 0
    jne .out
    mov si, ct_s_noval
    call ct_toast
    jmp .out
.rerr:
    mov si, ct_s_readerr
    call ct_toast
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_load_common - read [ct_name] whole into [ct_stgseg]; out: CF=0 and
; ES=[ct_stgseg]/CX=bytes read (ready for a reader to walk), or CF=1 on a
; file error. Clobbers ax, bx, dx, si.
; -----------------------------------------------------------------------------
ct_load_common:
    push ax
    push bx
    push dx
    push si
    mov es, [ct_stgseg]
    xor bx, bx
    mov cx, CT_CLAIM_STG_KB * 1024
    xor dx, dx
    mov si, ct_name
    call OSAPI_FILE_READ                ; out: DX:AX = bytes read, or CF=1
    jc .out
    mov cx, ax                          ; a file this small never exceeds 64KB
    clc
.out:
    pop si
    pop dx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_read_by_ext - load [ct_name] and run the reader its extension names.
; out: CF=1 = the file could not be read. BX is preserved (every one of these
; banks it - 82.10), so a caller may keep its window pointer there.
; -----------------------------------------------------------------------------
ct_read_by_ext:
    push si
    push di
    mov si, ct_name
    mov di, ct_s_ext_dif
    call ct_nameends
    jc .dif
    mov si, ct_name
    mov di, ct_s_ext_biff
    call ct_nameends
    jc .biff
    call ct_load_common
    jc .err
    call ct_read_sylk
    jmp .ok
.dif:
    call ct_load_common
    jc .err
    call ct_read_dif
    jmp .ok
.biff:
    call ct_load_common
    jc .err
    call ct_read_biff
.ok:
    pop di
    pop si
    clc
    ret
.err:
    pop di
    pop si
    stc
    ret

; -----------------------------------------------------------------------------
; ct_reread - read the open file again under the current [ct_wantcol] and
; redraw. in: SI = our window ptr. Preserves everything.
; -----------------------------------------------------------------------------
ct_reread:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    cmp byte [ct_name], 0
    je .out                             ; nothing open: the choice is remembered
    mov bx, si                          ; and applies to the next Open
    call ct_read_by_ext
    jc .err
    call ct_render
    mov si, bx                          ; BX survives the readers AND ct_render
    call ct_paint                       ; now - which is the whole of 82.10
    cmp word [ct_valcnt], 0
    jne .out
    mov si, ct_s_nocol
    call ct_toast
    jmp .out
.err:
    mov si, ct_s_readerr
    call ct_toast
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_expdlg - the Export dialog's completion proc: writes the current
; chart buffer via apps/os88chart.inc's ch_bmp_write.
; -----------------------------------------------------------------------------
ct_expdlg:
    push ax
    push bx
    push cx
    push si
    push di
    mov si, di
    mov di, ct_name
    mov cx, CT_NAMEMAX                  ; the count lives in CX - the loop
.copy:                                  ; body writes AL, so AX cannot hold it
    mov al, [es:si]
    mov [di], al
    or al, al
    jz .copied
    inc si
    inc di
    dec cx
    jnz .copy
    mov byte [di], 0
.copied:
    mov es, [ct_chartseg]
    mov bx, [ct_stgseg]
    mov si, ct_name
    call ch_bmp_write
    jnc .ok
    mov si, ct_s_experr
    call ct_toast
    jmp .out
.ok:
    mov si, ct_s_exported
    call ct_toast
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_nameends - in: SI=name (NUL-terminated), DI=suffix (NUL-terminated);
; out: CF=1 if name ends with suffix (case-sensitive: 8.3 names arrive
; already uppercase from the kernel, and so do the suffixes this file
; compares against)
; -----------------------------------------------------------------------------
ct_nameends:
    push ax
    push bx
    push cx
    push si
    push di
    xor cx, cx
    mov bx, si
.namelen:
    cmp byte [bx], 0
    je .havenamelen
    inc bx
    inc cx
    jmp .namelen
.havenamelen:
    push cx
    xor cx, cx
    mov bx, di
.suflen:
    cmp byte [bx], 0
    je .havesuflen
    inc bx
    inc cx
    jmp .suflen
.havesuflen:
    pop bx                              ; bx = strlen(name), cx = strlen(sfx)
    cmp cx, bx
    ja .no
    mov ax, si
    add ax, bx
    sub ax, cx
    mov si, ax
.cmp:
    or cx, cx
    jz .yes
    mov al, [si]
    cmp al, [di]
    jne .no
    inc si
    inc di
    dec cx
    jmp .cmp
.yes:
    stc
    jmp .out
.no:
    clc
.out:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_pint - parse a signed decimal integer
; in: ES:SI=ptr, BX=limit (exclusive, an offset); also stops at NUL
; out: AX=value, SI=advanced; BX preserved; ES must be set by the caller
; -----------------------------------------------------------------------------
ct_pint:
    push bx
    push cx
    push dx
    xor cx, cx
    xor ax, ax
    cmp si, bx
    jae .fin
    cmp byte [es:si], '-'
    jne .digits
    mov cx, 1
    inc si
.digits:
    cmp si, bx
    jae .fin
    mov dl, [es:si]
    or dl, dl
    jz .fin
    cmp dl, '0'
    jb .fin
    cmp dl, '9'
    ja .fin
    sub dl, '0'
    xor dh, dh
    push dx
    push bx
    mov bx, 10
    mul bx
    pop bx
    pop dx
    add ax, dx
    inc si
    jmp .digits
.fin:
    or cx, cx
    jz .nosign
    neg ax
.nosign:
    pop dx
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; ct_esatof (stage 4.6) - the decimal number at ES:SI (bounded by BX) becomes
; the packed double in ch_dbl; SI advances past it. os88fp.inc's fp_atof reads
; DS, and every one of these readers has the file staged in ES, so the text is
; copied into a DS scratch first - the same shape sheet.asm's sh_esatof uses,
; and for the same reason.
; -----------------------------------------------------------------------------
ct_esatof:
    push ax
    push cx
    push di
    mov di, ct_ntxt
    mov cx, CT_NTXT_MAX
.copy:
    jcxz .done
    cmp si, bx
    jae .done
    mov al, [es:si]
    cmp al, ';'
    je .done
    cmp al, ','
    je .done
    cmp al, 13
    je .done
    cmp al, 10
    je .done
    or al, al
    jz .done
    mov [di], al
    inc di
    inc si
    dec cx
    jmp .copy
.done:
    mov byte [di], 0
    push si
    push es
    mov si, ct_ntxt
    mov ax, ds                        ; fp_atof is DS-only, and ES is the
    mov es, ax                        ; staging segment right now
    call fp_atof
    pop es
    pop si
    mov di, ch_dbl
    call fp_pack_a
    pop di
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_i32_dbl - the SIGNED 32-bit integer in DX:AX becomes ch_dbl. RK's integer
; subtype is THIRTY bits, so a word was never enough for it either.
; -----------------------------------------------------------------------------
ct_i32_dbl:
    push ax
    push bx
    push cx
    push dx
    push di
    xor cx, cx                        ; CL = the sign
    or dx, dx
    jns .abs
    mov cl, 1
    neg ax                            ; negate DX:AX
    adc dx, 0
    neg dx
.abs:
    mov [fp_t0], ax
    mov [fp_t1], dx
    mov word [fp_t2], 0
    mov word [fp_t3], 0
    call fp_u64_to_a
    mov [fp_as], cl
    mov di, ch_dbl
    call fp_pack_a
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_finalize - given ct_trow/ct_tcol/ct_tval (ct_tcnt entries, any file
; order, any columns), find the LOWEST column among them, keep only the
; entries at that column, sort those by row ascending, and set
; ct_vals/ct_valcnt (capped at CH_MAXBARS) - the shared last step for all
; three readers below.
; -----------------------------------------------------------------------------
; ct_record - offer one cell to the series (the CT_TCAP fix)
; in:  AX = col, BX = row, and THE VALUE IN ch_dbl - eight bytes, not a word
; in DX. Stage 4.6: a cell holds an IEEE-754 double, and truncating it here
; was the whole of why 43.6 charted as a bar of 43 (82.13). All registers
; preserved.
;
; THE CAP USED TO BOUND THE SCAN, AND THAT LOST DATA SILENTLY. Each reader
; collected every numeric cell it met into ct_trow/ct_tcol/ct_tval, stopped at
; CT_TCAP of them, and only then did ct_finalize pick the lowest column and
; filter to it. On a wide sheet the temp arrays filled with OTHER columns'
; cells, so two things went wrong at once and neither announced itself: the
; tail of the chosen column was never read, and - worse - ct_mincol was derived
; from a truncated sample, so a lower column appearing later in the file was
; never seen and THE WRONG COLUMN WAS CHARTED. Both produced a plausible chart.
;
; So the filter runs as the file is read instead. The lowest column seen so far
; is the series; a cell BELOW it restarts the collection, a cell IN it is
; appended, a cell ABOVE it is dropped. One pass still, no second read, and the
; cap now bounds the KEPT SERIES rather than the scanned candidates - which is
; why it is CH_MAXBARS here and not CT_TCAP.
; -----------------------------------------------------------------------------
ct_record:
    push ax
    push bx
    push cx
    push si
    mov cx, [ct_wantcol]                ; Data > Column: anything to the LEFT of
    or cx, cx                           ; the chosen column is not a candidate,
    je .anycol                          ; so the chosen one becomes the lowest
    dec cx                              ; and the existing two-lowest logic
    cmp ax, cx                          ; picks the next one along as series 2.
    jb .out                             ; THE MENU IS 1-BASED AND AX IS NOT -
.anycol:                                ; ct_parse_c's .apply already did the
                                        ; dec - comparing the two directly
                                        ; charted the column after the one
                                        ; asked for
    cmp word [ct_tcnt], 0
    je .newcol                        ; nothing yet: this cell defines it
    cmp ax, [ct_mincol]
    je .append
    jb .newcol                        ; a LOWER column supersedes everything
    ; --- higher than the series: it may still be the SECOND one -------------
    ; Scatter and Combination need two (SPEC.md 82.8), so the next-lowest
    ; column is kept as well. The same three-way test, one level along.
    cmp word [ct_t2cnt], 0
    je .new2
    cmp ax, [ct_mincol2]
    ja .out
    je .append2
.new2:
    mov [ct_mincol2], ax
    mov word [ct_t2cnt], 0
.append2:
    mov cx, [ct_t2cnt]
    cmp cx, CH_MAXBARS
    jae .out
    mov si, cx
    shl si, 1
    mov [ct_t2row + si], bx
    mov si, cx
    call ct_doff
    add si, ct_t2val                    ; SI = &ct_t2val[cx]
    push di
    mov di, si
    mov si, ch_dbl
    call ch_sc_copy8
    pop di
    inc word [ct_t2cnt]
    jmp .out
.newcol:                              ; the old series becomes the second one,
    push ax                           ; rather than being thrown away - it IS
    push bx                           ; the next-lowest column by construction
    push dx
    mov ax, [ct_mincol]
    cmp word [ct_tcnt], 0
    je .nodemote
    mov [ct_mincol2], ax
    mov cx, [ct_tcnt]
    mov [ct_t2cnt], cx
    xor si, si
.demote:
    jcxz .nodemote
    mov ax, [ct_trow + si]
    mov [ct_t2row + si], ax
    push si
    push di
    shr si, 1
    call ct_doff
    mov di, si
    add si, ct_tval                     ; from ct_tval[i]...
    add di, ct_t2val                    ; ...to ct_t2val[i]
    call ch_sc_copy8
    pop di
    pop si
    add si, 2
    dec cx
    jmp .demote
.nodemote:
    pop dx
    pop bx
    pop ax
    mov [ct_mincol], ax
    mov word [ct_tcnt], 0
.append:
    mov cx, [ct_tcnt]
    cmp cx, CH_MAXBARS
    jae .out                          ; the series is full; a longer column is
                                       ; truncated, which ct_finalize's own
                                       ; CH_MAXBARS limit already implied
    mov si, cx
    shl si, 1
    mov [ct_tcol + si], ax
    mov [ct_trow + si], bx
    push si
    push di
    shr si, 1
    call ct_doff
    mov di, si
    add di, ct_tval                     ; DI = &ct_tval[cx]
    mov si, ch_dbl
    call ch_sc_copy8
    pop di
    pop si
    inc word [ct_tcnt]
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
ct_finalize:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [ct_valcnt], 0
    cmp word [ct_tcnt], 0
    je .done
                                        ; ct_mincol is ALREADY the lowest
                                        ; column and every collected cell is
                                        ; already in it - ct_record maintained
                                        ; both as the file was read, so the
                                        ; scan that used to derive it here is
                                        ; gone. The column test below is kept
                                        ; as a cheap invariant check rather
                                        ; than as a filter that still does
                                        ; work.
    xor cx, cx
.collect:
    cmp cx, [ct_tcnt]
    jae .sortit
    mov ax, [ct_valcnt]
    cmp ax, CH_MAXBARS
    jae .sortit
    mov si, cx
    shl si, 1
    mov bx, [ct_tcol + si]
    cmp bx, [ct_mincol]
    jne .cnext
    mov dx, [ct_trow + si]
    shr si, 1                           ; SI = the candidate's index
    push si
    call ct_doff
    add si, ct_tval                     ; -> &ct_tval[i]
    mov ax, [ct_valcnt]
    push si
    mov si, ax
    call ct_doff
    mov di, si
    add di, ct_vals                     ; -> &ct_vals[valcnt]
    pop si
    call ch_sc_copy8
    pop si
    mov ax, [ct_valcnt]
    mov si, ax
    shl si, 1
    mov [ct_vrow + si], dx
    inc word [ct_valcnt]
.cnext:
    inc cx
    jmp .collect
.sortit:                                ; insertion sort, ct_vrow/ct_vals
    mov cx, 1                           ; together, ascending by row - at
.outer:                                 ; most CH_MAXBARS=40 items, so an
    cmp cx, [ct_valcnt]                 ; O(n^2) sort costs nothing that
    jae .done                           ; matters here
    mov si, cx
    shl si, 1
.inner:
    or si, si
    jz .outernext
    mov ax, [ct_vrow + si]
    mov bx, [ct_vrow + si - 2]
    cmp ax, bx
    jae .outernext
    xchg ax, bx
    mov [ct_vrow + si], ax
    mov [ct_vrow + si - 2], bx
    push si                             ; and the eight bytes that belong with
    shr si, 1                           ; the row, swapped the same way
    call ct_doff
    add si, ct_vals                     ; ct_vals, NOT ct_tval - the sort runs
    mov di, si                          ; on the COLLECTED series
    sub di, 8
    push cx                             ; the sort's OUTER index lives in CX -
    mov cx, 4                           ; counting the four words in it reset
.swap8:                                 ; the outer walk after every swap
    mov ax, [si]
    mov bx, [di]
    mov [si], bx
    mov [di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .swap8
    pop cx
    pop si
    sub si, 2
    jmp .inner
.outernext:
    inc cx
    jmp .outer
.done:
    ; --- the doubles become the words the drawing runs on (82.13) ----------
    ; SERIES TWO FIRST, so [ch_e10] is left holding SERIES ONE's exponent -
    ; that is the one the value axis is labelled from, and the second series
    ; is drawn against its own ch_max2 with no scale of its own.
    mov dx, ds
    mov si, ct_t2val
    mov di, ct_w2vals
    mov cx, [ct_t2cnt]
    call ch_scale
    mov dx, ds
    mov si, ct_vals
    mov di, ct_wvals
    mov cx, [ct_valcnt]
    call ch_scale
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ct_doff - SI = SI * 8, the byte offset of the SI'th double. The CALLER adds
; the array's base: an earlier version folded ct_tval in here and two of its
; four call sites wanted a different array, so the second series and the sort
; both addressed the first one. Everything else preserved.
ct_doff:
    push ax
    push dx
    mov ax, si
    mov dx, 8
    mul dx
    mov si, ax
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_rkdec (stage 4.6) - ALL FOUR RK SUBTYPES, into ch_dbl.
; in: DX:AX = a packed RK value (AX low word, DX high word). Always succeeds.
;
; The two low bits are the subtype. Bit 1 set means the upper 30 bits are a
; signed integer; clear means the 32-bit value with those two bits masked off
; IS THE TOP HALF of an IEEE-754 double, low half zero. Bit 0 set means divide
; the result by 100 either way.
;
; This used to accept the integer-times-one form ALONE and skip the cell
; otherwise - which was defensible while the array was signed words and the
; other three forms could only have been guessed at. It is not defensible now:
; Sheet writes real doubles, and "skip the cell" meant a column of 43.6 and
; 44.1 charted as an EMPTY sheet with no message.
; -----------------------------------------------------------------------------
ct_rkdec:
    push ax
    push bx
    push cx
    push dx
    push si                             ; the caller's record-walk cursor -
    push di                             ; .div100 needs SI for fp_unpack_a
    mov bl, al                          ; the subtype bits, banked
    test al, 0x02
    jz .isfloat
    mov cx, 2                           ; a signed 30-bit integer
.shr:
    sar dx, 1
    rcr ax, 1
    loop .shr
    call ct_i32_dbl
    jmp .div100
.isfloat:
    and al, 0xFC                        ; the top 32 bits of a double
    mov word [ch_dbl], 0
    mov word [ch_dbl+2], 0
    mov [ch_dbl+4], ax
    mov [ch_dbl+6], dx
.div100:
    test bl, 0x01
    jz .out
    mov si, ch_dbl
    call fp_unpack_a
    mov cx, -2                          ; /100, exactly as scaling by 10^-2
    call fp_scale10
    mov di, ch_dbl
    call fp_pack_a
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    clc
    ret

; -----------------------------------------------------------------------------
; ct_reset_series - both series' collection state, zeroed. Every reader
; calls this first: ct_t2cnt/ct_mincol2 are written only inside ct_record,
; so a reader that zeroed just ct_tcnt carried the PREVIOUS file's second
; column into this one's chart. Preserves everything.
; -----------------------------------------------------------------------------
ct_reset_series:
    mov word [ct_tcnt], 0
    mov word [ct_t2cnt], 0
    mov word [ct_mincol2], 0
    ret

; -----------------------------------------------------------------------------
; ct_read_biff - in: ES=[ct_stgseg], CX=byte length already read there.
; Walks real [opcode:word][length:word] BIFF record headers; on an RK cell
; record (0x027E: row,col,xf,rk_lo,rk_hi, 10 bytes) decodes the value via
; ct_rkdec and records (row,col,value) for ct_finalize, capped at
; ct_record, which keeps only the lowest column. Stops at EOF (0x000A) or
; a truncated trailing record.
; -----------------------------------------------------------------------------
ct_read_biff:
    push ax
    push bx
    push dx
    push si
    mov [ct_biffend], cx                ; the walk's end bound, banked - the
    call ct_reset_series                ; NUMBER path needs CX as a counter
    xor si, si
.rechdr:
    mov ax, si
    add ax, 4
    jc .done                            ; a wrapped sum passes the compare
    cmp ax, cx
    ja .done
    mov ax, [es:si]                     ; opcode
    mov dx, [es:si+2]                   ; length
    add si, 4
    cmp ax, 0x000A                      ; EOF
    je .done
    cmp ax, 0x027E                      ; RK cell record
    je .isrk
    cmp ax, 0x0203                      ; NUMBER: eight bytes of IEEE-754,
    je .isnum                           ; verbatim, and the ONLY way a value
    jmp .skip                           ; that is not an exact small integer
.isrk:                                  ; reaches a BIFF file at all
    cmp dx, 10                          ; too short to hold row/col/xf/rk:
    jb .skip                            ; stale buffer bytes are not a value
    push dx                             ; length, saved across the decode
    mov ax, si
    add ax, dx
    jc .toolong                         ; a wrapped sum passes the compare
    cmp ax, cx
    ja .toolong
    push word [es:si]                   ; row
    push word [es:si+2]                 ; col
    mov ax, [es:si+6]                   ; rk lo
    mov dx, [es:si+8]                   ; rk hi
    call ct_rkdec                       ; -> ch_dbl
    pop ax                              ; ax = col
    pop bx                              ; bx = row
    call ct_record
    pop dx                              ; length, restored
    jmp .skip
.isnum:
    cmp dx, 14                          ; too short to hold row/col/xf plus
    jb .skip                            ; the eight bytes
    push dx
    mov ax, si
    add ax, dx
    jc .toolong                         ; a wrapped sum passes the compare
    cmp ax, cx
    ja .toolong
    push word [es:si]                   ; row
    push word [es:si+2]                 ; col
    push di
    push si
    add si, 6                           ; past row/col/xf: the eight bytes
    mov di, ch_dbl
    mov cx, 4
.ncopy:
    mov ax, [es:si]
    mov [di], ax
    add si, 2
    add di, 2
    dec cx
    jnz .ncopy
    pop si
    pop di
    pop ax                              ; col
    pop bx                              ; row
    ; CX was the buffer END and .ncopy just ate it - restore it from the
    ; record walk's own bookkeeping before the next header is read.
    call ct_record
    pop dx
    jmp .nskip
.toolong:
    pop dx                              ; length, restored (discard)
    jmp .done
.nskip:
    mov cx, [ct_biffend]                ; .ncopy used CX as a counter, so the
                                        ; walk's own end bound is re-read here
.skip:
    mov ax, si                          ; the advance is bounds-checked HERE,
    add ax, dx                          ; not just per record type: a hostile
    jc .done                            ; length near 0xFFFF wraps SI back onto
    cmp ax, cx                          ; the same header and the walk never
    ja .done                            ; ends - on the UI task with the gfx
    mov si, ax                          ; lock held, that is the whole desktop
    jmp .rechdr
.done:
    pop si
    pop dx
    pop bx
    pop ax
    call ct_finalize
    ret

; -----------------------------------------------------------------------------
; ct_read_sylk - in: ES=[ct_stgseg], CX=byte length already read there.
; Line-oriented: any line shaped "C;<tokens>" is a candidate cell record.
; Tokens are order-independent, ';'-separated, 1-based X (col)/Y (row)/K
; (value) - real SYLK's own C-record grammar. Only a line carrying an
; explicit K is recorded (an omitted X or Y is treated as invalid, not
; "sticky" from a prior line - the same simplification Sheet's own
; sh_parsecrec makes). Records (row,col,value) for ct_finalize, capped at
; ct_record, which keeps only the lowest column.
; -----------------------------------------------------------------------------
ct_read_sylk:
    push ax
    push bx
    push dx
    push si
    push di
    call ct_reset_series
    mov di, cx                          ; di = end offset
    xor si, si
.lineloop:
    cmp si, di
    jae .done
    mov bx, si
.findeol:
    cmp bx, di
    jae .goteol
    mov al, [es:bx]
    cmp al, 13
    je .goteol
    cmp al, 10
    je .goteol
    inc bx
    jmp .findeol
.goteol:
    mov ax, bx
    sub ax, si
    cmp ax, 2
    jb .advance
    cmp byte [es:si], 'C'
    jne .advance
    cmp byte [es:si+1], ';'
    jne .advance
    push si
    add si, 2
    call ct_parse_c                     ; in: si=tokens start, bx=line end
    pop si
.advance:
    mov si, bx
.skipterm:
    cmp si, di
    jae .lineloop
    mov al, [es:si]
    cmp al, 13
    je .isterm
    cmp al, 10
    je .isterm
    jmp .lineloop
.isterm:
    inc si
    jmp .skipterm
.done:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    call ct_finalize
    ret

; -----------------------------------------------------------------------------
; ct_parse_c - the fields of one 'C' record; in: SI=tokens start (right
; after "C;"), BX=line end (exclusive); ES=[ct_stgseg], same buffer
; ct_read_sylk is walking
; -----------------------------------------------------------------------------
ct_parse_c:
    push ax
    push bx
    push cx
    push dx
    push si
    mov word [ct_pcol], 0
    mov word [ct_prow], 0
    mov word [ct_pval], 0
    mov byte [ct_phave], 0
.tok:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    cmp al, ';'
    je .skipsemi
    cmp al, 'X'
    je .isx
    cmp al, 'Y'
    je .isy
    cmp al, 'K'
    je .isk
.scan:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    inc si
    cmp al, ';'
    jne .scan
    jmp .tok
.skipsemi:
    inc si
    jmp .tok
.isx:
    inc si
    call ct_pint
    mov [ct_pcol], ax
    jmp .tok
.isy:
    inc si
    call ct_pint
    mov [ct_prow], ax
    jmp .tok
.isk:
    inc si
    cmp si, bx
    jae .tok
    cmp byte [es:si], '"'               ; K"..." is a LABEL, not a number, and
    je .istext                          ; a label is not a data point
    cmp byte [es:si], '#'               ; ...and ;K#DIV/0! is an ERROR VALUE
    je .tok                             ; (81.20.1), which is not one either
    call ct_esatof                      ; -> ch_dbl
    push si
    push di
    mov si, ch_dbl
    mov di, ct_pval
    call ch_sc_copy8
    pop di
    pop si
    mov byte [ct_phave], 1
    jmp .tok
.istext:
    ; SKIP IT, recording nothing. ct_pint would have parsed the opening quote
    ; as the number 0, so a column of row headings charted as a row of zero
    ; bars and every header cell became a spurious leading zero in its own
    ; column. The other two readers already got this right - BIFF records only
    ; RK (numeric) cells and never LABEL, and the DIF reader skips its type 1
    ; - so SYLK was the one that turned text into data.
    inc si                              ; past the opening quote
.txtskip:
    cmp si, bx
    jae .apply
    mov al, [es:si]
    inc si
    cmp al, '"'
    jne .txtskip
    jmp .tok
.apply:
    cmp byte [ct_phave], 0
    je .out
    mov ax, [ct_pcol]
    cmp ax, 1
    jb .out
    mov cx, [ct_prow]
    cmp cx, 1
    jb .out
    dec ax                              ; 1-based -> 0-based
    dec cx
    mov bx, cx                          ; bx = row, ax = col
    push si
    push di
    mov si, ct_pval
    mov di, ch_dbl
    call ch_sc_copy8
    pop di
    pop si
    call ct_record
.out:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_difskipline - advance SI past the rest of the current line and every
; trailing CR/LF (DI = end offset, module-scoped like ct_read_dif's own)
; -----------------------------------------------------------------------------
ct_difskipline:
    push ax
.scan:
    cmp si, di
    jae .out
    mov al, [es:si]
    inc si
    cmp al, 13
    je .eat
    cmp al, 10
    je .eat
    jmp .scan
.eat:
    cmp si, di
    jae .out
    mov al, [es:si]
    cmp al, 13
    je .eat2
    cmp al, 10
    je .eat2
    jmp .out
.eat2:
    inc si
    jmp .eat
.out:
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_is_bot_line - in: SI=line start, DI=end (exclusive); out: CF=1 if the
; line at SI is exactly "BOT" (the real DIF row marker), else CF=0. Does
; not advance SI.
; -----------------------------------------------------------------------------
ct_is_bot_line:
    push ax
    push bx
    mov bx, si
    add bx, 3
    cmp bx, di
    ja .no
    cmp byte [es:si], 'B'
    jne .no
    cmp byte [es:si+1], 'O'
    jne .no
    cmp byte [es:si+2], 'T'
    jne .no
    cmp bx, di
    jae .yes
    mov al, [es:bx]
    cmp al, 13
    je .yes
    cmp al, 10
    je .yes
    jmp .no
.yes:
    stc
    jmp .out
.no:
    clc
.out:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_read_dif - in: ES=[ct_stgseg], CX=byte length already read there.
; Skips the header STRUCTURALLY - unlike Sheet's own closed-loop DIF
; reader, which safely assumes its own writer's fixed 12-line header, this
; reads files it did not write, so it scans line by line for the first
; line that is exactly "BOT" (the real DIF row marker) rather than
; assuming any particular header length. From there, walks rows exactly
; like the grammar this project's own writer emits (each row: "-1,0" then
; "BOT"; each cell: "0,<value>" then "V", or anything else, meaning
; NA/blank). Offers (row,col,value) to ct_record, which keeps the lowest
; column and caps the series at CH_MAXBARS.
; -----------------------------------------------------------------------------
ct_read_dif:
    push ax
    push bx
    push dx
    push si
    push di
    call ct_reset_series
    mov di, cx                          ; di = end offset
    xor si, si
.hdrscan:
    cmp si, di
    jae .done                           ; no BOT anywhere: no data
    call ct_is_bot_line
    jc .foundbot
    call ct_difskipline
    jmp .hdrscan
.foundbot:
    call ct_difskipline                 ; consume the first row's BOT line
    mov word [ct_wrow], 0
    mov word [ct_wcol], 0
    jmp .cellloop
.rowloop:
    cmp si, di
    jae .done
    call ct_difskipline                 ; the "-1,0" line
    cmp si, di
    jae .done
    mov al, [es:si]
    cmp al, 'E'                         ; EOD
    je .done
    call ct_difskipline                 ; the "BOT" line
    mov ax, [ct_wrow]
    inc ax
    mov [ct_wrow], ax
    mov word [ct_wcol], 0
.cellloop:
    cmp si, di
    jae .done
    mov al, [es:si]
    cmp al, '-'
    je .rowloop                         ; the next row's "-1,0"
    cmp al, '0'
    jne .skipunknown                    ; type 1 (string/NA) or unknown
    add si, 2                           ; past "0,"
    mov bx, di
    call ct_esatof                      ; -> ch_dbl, si past the digits
    push si
    push di
    mov si, ch_dbl
    mov di, ct_pval
    call ch_sc_copy8
    pop di
    pop si
    call ct_difskipline                 ; finish the "0,<value>" line
    cmp si, di
    jae .cellnext
    cmp byte [es:si], 'V'               ; the real DIF value-indicator
    jne .notvalid
    mov bx, [ct_wrow]
    mov ax, [ct_wcol]
    push si
    push di
    mov si, ct_pval
    mov di, ch_dbl
    call ch_sc_copy8
    pop di
    pop si
    call ct_record
.notvalid:
    call ct_difskipline                 ; the indicator line
    jmp .cellnext
.skipunknown:
    call ct_difskipline
    cmp si, di
    jae .cellnext
    call ct_difskipline                 ; every cell is exactly two lines
.cellnext:
    mov ax, [ct_wcol]
    inc ax
    mov [ct_wcol], ax
    jmp .cellloop
.done:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    call ct_finalize
    ret

; --- window template (SPEC.md 11: 16 bytes, 8 words) ---------------------------
ct_tpl:
    dw 0, 0, CT_WIN_W, CT_WIN_H
    dw ct_s_title, ct_paint, 0, ct_onclick  ; no onkey; the click is the About
                                        ; card's dismissal and nothing else

; --- the app menu set (SPEC.md 12.2) -------------------------------------------
    OS88_MENUSET ct_menus, ct_name_app, ct_oncmd
        OS88_MENU ct_m_file, ct_i_file, 2
        OS88_MENU ct_m_gallery, ct_i_gallery, 7
        OS88_MENU ct_m_data, ct_i_data, 9
    OS88_MENUSET_END ct_menus

ct_name_app: db 'Chart', 0
; WHICH COLUMN TO CHART. Excel needs no such menu because it charts a
; SELECTION; this app opens a FILE, so nothing in it says which column was
; meant and the reader can only fall back on "the lowest one". That is right
; for a sheet of figures and wrong for one whose first column is a year or an
; index, so it is offered as a choice rather than guessed (SPEC.md 82.11).
; Automatic is the old behaviour and stays the default.
ct_m_data:   db 'Data', 0
ct_i_data:   dw ct_it_auto, ct_it_ca, ct_it_cb, ct_it_cc, ct_it_cd, ct_it_ce, ct_it_cf, ct_it_cg, ct_it_ch
ct_it_auto:  db 'Automatic', 0
ct_it_ca:    db 'Column A', 0
ct_it_cb:    db 'Column B', 0
ct_it_cc:    db 'Column C', 0
ct_it_cd:    db 'Column D', 0
ct_it_ce:    db 'Column E', 0
ct_it_cf:    db 'Column F', 0
ct_it_cg:    db 'Column G', 0
ct_it_ch:    db 'Column H', 0
ct_s_nocol:  db 'No data in that column.', 0

ct_m_file:   db 'File', 0
ct_i_file:   dw ct_it_open, ct_it_exp
ct_it_open:  db 'Open...', 0
ct_it_exp:   db 'Export as BMP...', 0

; Excel 2.1d's Gallery menu is Area/Bar/Column/Line/Pie/Scatter/Combination.
; ALL SEVEN now. Scatter and Combination needed a second series, which this
; reader supplies by keeping the two lowest-numbered columns rather than only
; the lowest (82.8) - the data-model problem that kept them out until now. THE ORDER MATCHES
; ct_gal_map below, which is indexed by the item number - keep them in step.
ct_m_gallery: db 'Gallery', 0
ct_i_gallery: dw ct_it_area, ct_it_bar, ct_it_col, ct_it_line, ct_it_pie, ct_it_sca, ct_it_cmb
ct_it_area:   db 'Area', 0
ct_it_bar:    db 'Bar', 0
ct_it_col:    db 'Column', 0
ct_it_line:   db 'Line', 0
ct_it_pie:    db 'Pie', 0
ct_it_sca:    db 'Scatter', 0
ct_it_cmb:    db 'Combination', 0

ct_gal_map:    dw CH_T_AREA, CH_T_BAR, CH_T_COLUMN, CH_T_LINE, CH_T_PIE, CH_T_SCATTER, CH_T_COMBO
ct_s_title:    db 'Chart', 0
; --- the About card's lines (SPEC.md 20.5.1) ----------------------------------
; The content box is CT_WIN_W - 2 = 258px, so 30 cells less the card's margins.
ct_ablines:
    dw ct_ab1, ct_ab2, ct_ab3, ct_ab4, 0
ct_ab1:        db 'Chart for os8088', 0
ct_ab2:        db 'Charts a column of a sheet', 0
ct_ab3:        db 0
ct_ab4:        db 'Contributed by Koriban', 0
ct_s_chartbmp: db 'CHART.BMP', 0
ct_s_noexp:    db 'No chart to export.', 0
ct_s_experr:   db 'Chart export failed.', 0
ct_s_exported: db 'Chart exported.', 0
ct_s_readerr:  db 'Could not read that file.', 0
ct_s_noval:    db 'No numeric data found.', 0
ct_s_ext_dif:  db '.DIF', 0
ct_s_ext_biff: db '.BIF', 0

; stage: shared rasterizer + BMP writer - see that file's own header
; comment for the CH_* constants and ch_* bss words it requires, both
; declared above. Included here, just before OS88_BSS, for the same
; fixed-offset reason its own header states: code between the header and
; here would break the icon macro's fixed-offset assertion (this package
; has no icon, but the same %include-at-the-end rule still applies) and
; would move the entry point.
%include "os88fp.inc"                  ; stage 4.6: the readers meet real
                                       ; decimals now (a BIFF NUMBER record IS
                                       ; an IEEE-754 double), and ch_scale
                                       ; below needs it. Before os88chart.inc,
                                       ; which calls into it.
%include "os88chart.inc"

; --- the shared controls (SPEC.md 20.5.1) -------------------------------------
%define OS88UI_ABOUT                   ; the standard About card, and NOTHING
%define OS88UI_NOBTN                   ; else: this package draws no button
%include "os88ui.inc"

; =============================================================================
; bss (loader-zeroed, SPEC.md 21 step 5)
; =============================================================================
    OS88_BSS 1832
    OS88_IMAGE_END

ct_chartseg equ os88_image_end + 0  ; word: the offscreen canvas claim
ct_stgseg   equ ct_chartseg + 2     ; word: file-read/BMP-export staging
ct_name     equ ct_stgseg + 2       ; 13: the opened/exported file's 8.3 name
ct_valcnt   equ ct_name + 13        ; word: values currently charted
ct_vals     equ ct_valcnt + 2       ; CH_MAXBARS DOUBLES: the charted values
ct_vrow     equ ct_vals + CH_MAXBARS*8   ; CH_MAXBARS words: scratch rows,
                                          ; paired with ct_vals during
                                          ; ct_finalize's sort, unused after
ct_mincol   equ ct_vrow + CH_MAXBARS*2   ; word: ct_finalize's own scratch
ct_tcnt     equ ct_mincol + 2       ; word: how many candidates are in
                                     ; ct_trow/ct_tcol/ct_tval right now
ct_trow     equ ct_tcnt + 2         ; CH_MAXBARS words: the series' rows
ct_tcol     equ ct_trow + CH_MAXBARS*2  ; ...their columns (all equal)
ct_tval     equ ct_tcol + CH_MAXBARS*2  ; ...and their values, as DOUBLES
ct_pcol     equ ct_tval + CH_MAXBARS*8  ; word: ct_parse_c's own scratch
ct_prow     equ ct_pcol + 2         ; word: ct_parse_c's own scratch
ct_pval     equ ct_prow + 2         ; 8: shared scratch (ct_parse_c AND
                                     ; ct_read_dif's own per-cell value -
                                     ; never live across both at once)
ct_phave    equ ct_pval + 8         ; byte: ct_parse_c's own scratch
ct_ntxt     equ ct_phave + 1        ; CT_NTXT_MAX+1: ct_esatof's DS copy of
                                     ; one number, out of the staged file
ct_biffend  equ ct_ntxt + CT_NTXT_MAX + 1  ; word: ct_read_biff's end bound
ct_wrow     equ ct_biffend + 2      ; word: ct_read_dif's own row counter
ct_wcol     equ ct_wrow + 2         ; word: ct_read_dif's own col counter

; --- apps/os88chart.inc's own required scratch (see its header comment) -------
ch_max      equ ct_wcol + 2
ch_base     equ ch_max + 2
ch_arr      equ ch_base + 2
ch_cnt      equ ch_arr + 2
ch_idx      equ ch_cnt + 2
ch_bx1      equ ch_idx + 2
ch_by1      equ ch_bx1 + 2
ch_bx2      equ ch_by1 + 2
ch_by2      equ ch_bx2 + 2
ch_srcseg   equ ch_by2 + 2
ch_stgseg   equ ch_srcseg + 2
ch_neg      equ ch_stgseg + 2     ; stage 3.0f: 1 = some value is
                                       ; negative. Its own word now: the axis
                                       ; row is type-dependent, so ch_base
                                       ; cannot carry this as well.
ch_type     equ ch_neg + 2       ; CH_T_* - which chart to draw
ch_lx0      equ ch_type + 2      ; the current segment's endpoints and
ch_ly0      equ ch_lx0 + 2       ; the column being interpolated -
ch_lx1      equ ch_ly0 + 2       ; CALLER bss like every other ch_*
ch_ly1      equ ch_lx1 + 2       ; word, for the same DS reason
ch_lcx      equ ch_ly1 + 2
ch_pie_px      equ ch_lcx + 2       ; --- stage 3.0f: the pie ---
ch_pie_py      equ ch_pie_px + 2
ch_pie_ex      equ ch_pie_py + 2    ; ch_ray's endpoint and its Bresenham
ch_pie_ey      equ ch_pie_ex + 2    ; state - in bss for the same DS reason
ch_pie_x       equ ch_pie_ey + 2    ; every other ch_* word is
ch_pie_y       equ ch_pie_x + 2
ch_pie_dx      equ ch_pie_y + 2
ch_pie_dy      equ ch_pie_dx + 2
ch_pie_sx      equ ch_pie_dy + 2
ch_pie_sy      equ ch_pie_sx + 2
ch_pie_err     equ ch_pie_sy + 2
ch_pie_e2      equ ch_pie_err + 2
ch_pie_tlo     equ ch_pie_e2 + 2    ; the 32-bit total and how far it was
ch_pie_thi     equ ch_pie_tlo + 2   ; shifted to fit a word
ch_pie_shift   equ ch_pie_thi + 2
ch_pie_a0      equ ch_pie_shift + 2 ; this slice's first half-degree...
ch_pie_span    equ ch_pie_a0 + 2    ; ...how many it covers...
ch_pie_a       equ ch_pie_span + 2  ; ...and the sweep's current one
ch_pie_col     equ ch_pie_a + 2
ch_pie_thick   equ ch_pie_col + 2    ; byte: this ray fills, so it is 3px
ch_pie_pen     equ ch_pie_thick + 1  ; byte: the colour ch_setpixel keeps
ch_pie_pat     equ ch_pie_pen + 1    ; byte: this slice's hatch, FF = solid
ch_tx          equ ch_pie_pat + 1   ; --- stage 3.0f: text into the canvas ---
ch_ty          equ ch_tx + 2
ch_tpen        equ ch_ty + 2
ch_tsrc        equ ch_tpen + 2        ; the string cursor, across ch_glyph
ch_tseg        equ ch_tsrc + 2        ; the GLYPH TABLE's segment, not KERNEL_SEG
ch_ttab        equ ch_tseg + 2
ch_tfirst      equ ch_ttab + 2        ; the character range the table covers
ch_tlast       equ ch_tfirst + 2
ch_tglyph      equ ch_tlast + 2       ; -> the current character's 8 rows
ch_trow        equ ch_tglyph + 2
ch_tcol        equ ch_trow + 2
ch_tpy         equ ch_tcol + 2
ch_tbits       equ ch_tpy + 2
ch_tnum        equ ch_tbits + 2       ; 16: ch_itoa_t's/ch_num_t's output -
                                      ; eight held "-32768" and nothing more,
                                      ; and a scaled label can carry a point
                                      ; and four digits, or nine trailing
                                      ; zeros (see ch_scale)
ch_e10         equ ch_tnum + 16     ; the series' decimal exponent (82.13)
ch_sc_seg      equ ch_e10 + 2       ; ch_scale's own scratch
ch_sc_src      equ ch_sc_seg + 2
ch_sc_dst      equ ch_sc_src + 2
ch_sc_cnt      equ ch_sc_dst + 2
ch_dbl         equ ch_sc_cnt + 2    ; 8: the value being converted...
ch_dmax        equ ch_dbl + 8       ; 8: ...and the largest seen
ch_title       equ ch_dmax + 8      ; -> the chart's title, or 0 for none
ch_legy        equ ch_title + 2     ; the legend row being drawn...
ch_legr        equ ch_legy + 2      ; ...and the swatch row inside it
ch_arr2        equ ch_legr + 2       ; --- the SECOND series (82.8) ---
ch_cnt2        equ ch_arr2 + 2      ; 0 = there is no second series
ch_srcseg2     equ ch_cnt2 + 2
ch_max2        equ ch_srcseg2 + 2   ; its own scale, independent of the first
ch_mkx         equ ch_max2 + 2      ; ch_mark's centre
ch_mky         equ ch_mkx + 2
ch_scx         equ ch_mky + 2       ; a scatter point's x, across the y maths
ch_cbx         equ ch_scx + 2       ; a combination point...
ch_cby         equ ch_cbx + 2
ch_lcy         equ ch_cby + 2       ; ...and the previous one's y
ch_l2x         equ ch_lcy + 2       ; ch_line2's Bresenham state
ch_l2y         equ ch_l2x + 2
ch_l2ex        equ ch_l2y + 2
ch_l2ey        equ ch_l2ex + 2
ch_l2dx        equ ch_l2ey + 2
ch_l2dy        equ ch_l2dx + 2
ch_l2sx        equ ch_l2dy + 2
ch_l2sy        equ ch_l2sx + 2
ch_l2err       equ ch_l2sy + 2
ch_l2e2        equ ch_l2err + 2
ct_mincol2  equ ch_l2e2 + 2         ; the SECOND series' column...
ct_t2cnt    equ ct_mincol2 + 2      ; ...how many cells it has...
ct_t2row    equ ct_t2cnt + 2        ; ...and its rows and values
ct_t2val    equ ct_t2row + CH_MAXBARS * 2   ; ...as DOUBLES, like ct_tval
ct_wvals    equ ct_t2val + CH_MAXBARS * 8   ; ch_scale's output: the signed
ct_w2vals   equ ct_wvals + CH_MAXBARS * 2   ; words the drawing reads, plus
                                             ; [ch_e10] to say what they mean
ct_wantcol  equ ct_w2vals + CH_MAXBARS * 2  ; word: 0 = chart the lowest
                                             ; column, else the 1-based column
                                             ; Data > Column asked for
fp_as             equ ct_wantcol + 2   ; --- os88fp.inc's caller-declared
fp_bs             equ fp_as + 1        ; storage, exactly as its header lists
fp_ae             equ fp_bs + 1        ; it and exactly as sheet.asm declares
fp_be             equ fp_ae + 2        ; it
fp_am0            equ fp_be + 2
fp_am1            equ fp_am0 + 2
fp_am2            equ fp_am1 + 2
fp_am3            equ fp_am2 + 2
fp_bm0            equ fp_am3 + 2
fp_bm1            equ fp_bm0 + 2
fp_bm2            equ fp_bm1 + 2
fp_bm3            equ fp_bm2 + 2
fp_t0             equ fp_bm3 + 2
fp_t1             equ fp_t0 + 2
fp_t2             equ fp_t1 + 2
fp_t3             equ fp_t2 + 2
fp_p0             equ fp_t3 + 2        ; 8 words: the 128-bit product
fp_sticky         equ fp_p0 + 16
fp_tmp            equ fp_sticky + 2
fp_dig            equ fp_tmp + 2       ; 24: fp_ftoa's digit string
fp_d10            equ fp_dig + 24
fp_nd             equ fp_d10 + 2
fp_sgn            equ fp_nd + 2
fp_sq             equ fp_sgn + 2       ; 8: fp_sqrt's input, across iterations
fp_g              equ fp_sq + 8        ; 8: its running guess
fp_tv             equ fp_g + 8         ; 8: fp_floor's general temporary
fp_hw             equ fp_tv + 8        ; --- the coprocessor path ---
fp_x1             equ fp_hw + 1        ; 10: A in 80-bit form
fp_x2             equ fp_x1 + 10       ; 10: B
fp_sw             equ fp_x2 + 10       ; where the status word lands
ct_abon     equ fp_sw + 2   ; byte: the About card is up (SPEC.md 20.5.1)
ct_bss_end  equ ct_abon + 1

; -----------------------------------------------------------------------------
; The bss size above is a PLAIN LITERAL that nothing cross-checks, and setting
; it low is silent corruption of whatever the loader placed next rather than a
; build error. It cannot be written as an expression: OS88_BSS_SIZE goes into
; the package header's dw at a FIXED OFFSET (SPEC.md 20.2), so it must be known
; on pass 1, and a forward reference to a label defined down here makes NASM
; size instructions differently per pass.
;
; So it stays a literal and this asserts it. A mismatch drives one of the two
; TIMES counts negative, which -w+error turns into a build failure naming the
; exact shortfall; both are zero when the literal is right, so nothing is
; emitted. READ THE LINE NUMBER, not just the sign - the two report the same
; shortfall with opposite signs, so which one fired is what says whether the
; literal is too small or too large.
; -----------------------------------------------------------------------------
%define CT_BSS_NEED (ct_bss_end - os88_image_end)
    times (CT_BSS_NEED - OS88_BSS_SIZE) db 0
    times (OS88_BSS_SIZE - CT_BSS_NEED) db 0
