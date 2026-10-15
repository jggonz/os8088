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
CT_STG_MAX_KB     equ 32            ; the largest file Chart reads. Its
                                     ; staging is claimed per read at the
                                     ; file's own size and freed before the
                                     ; callback returns (ct_stage); 32KB was
                                     ; the fixed claim that used to be held
                                     ; for the instance's whole life, and it
                                     ; stays the ceiling (SPEC.md 82.14)
CT_NAMEMAX equ 12                   ; 8.3 name, no NUL
; THE STAGING CLAIM'S LAYOUT (SPEC.md 82.14). The file is read at CT_FOFF and
; the series are collected BELOW it, in the same transient claim, because
; nothing in them outlives the read except the scaled words - which
; ct_finalize copies out into ct_w2vals/ct_wvals before the claim goes back.
; They were 800 bytes of the package's own bss, resident for the instance's
; whole life, to hold data that is live for one callback.
CT_FOFF     equ 1024                    ; the file; a multiple of 16, which
                                        ; OSAPI_FILE_READ requires of BX
CT_A_TROW   equ 0                       ; CH_MAXBARS words: series one's rows
CT_A_TVAL   equ CT_A_TROW + CH_MAXBARS*2    ; ...and its values, as DOUBLES
CT_A_T2VAL  equ CT_A_TVAL + CH_MAXBARS*8    ; series two's values (in the
                                            ; file's order: nothing sorts it)
CT_A_W      equ CT_A_T2VAL + CH_MAXBARS*8   ; ch_scale's words, series two
                                            ; then one - ct_w2vals' order
CT_A_END    equ CT_A_W + CH_MAXBARS*4
%if CT_A_END > CT_FOFF
    %error "the series no longer fit below the staged file"
%endif
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
; ct_entry - package entry point (SPEC.md 20.2). The canvas is claimed here
; (the one place a package has no window yet), the constant BMP header+palette
; are copied into it once (see os88chart.inc's own ch_hdrtpl comment: "copy
; this once ... ch_bmp_write just stages whatever is already sitting there"),
; then the window and its File menu are created.
;
; THE CANVAS IS THE ONLY CLAIM HELD FOR THE INSTANCE'S LIFE (SPEC.md 82.14).
; A second, 32KB staging claim used to be taken here as well and kept until
; the window closed, for two moments that each last one callback: reading a
; file, and exporting one. A read now claims what that file needs and gives it
; back before the callback returns (ct_stage), and an export needs no staging
; at all (ct_expdlg) - so an open Chart holds 19KB of heap where it held 51.
; The bss is loader-zeroed (SPEC.md 21 step 5), so nothing here clears the
; value count or the name.
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
    mov es, dx                          ; copy the constant 118-byte BMP
    mov si, ch_hdrtpl                   ; header+palette into the buffer
    xor di, di                          ; once, here - nothing ever rebuilds
    mov cx, CH_HDRSZ                    ; it, and the export writes it as it
    cld                                 ; stands
    rep movsb
    mov si, ct_tpl
    call OSAPI_WM_CREATE                ; BX = window ptr, CF on table full
    jc .fail
    ; OUR REGION MAY MOVE (SPEC.md 66.6.1). Here, where the window
    ; exists, and not beside any worker's declaration: a package with
    ; NO worker is the case that moves most easily, and putting it at
    ; the spawn left exactly those runs declaring nothing - measured,
    ; by the row that reads MC_RLOC back out of the kernel's own table.
    OS88_REGION_MOVABLE
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
; ct_render - rasterize ct_wvals[0..ct_valcnt) into ct_chartseg via
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
    xor ax, ax                          ; the file it charted, which is the
    cmp [ct_name], al                   ; only name this app has for the data
    je .titled                          ; - or no title at all before a file
    mov ax, ct_name
.titled:
    mov [ch_title], ax
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
; ct_oncmd - the app's menus (AH = the menu, AL = the item); SI = the owning
; window, gfx lock already held (SPEC.md 12.2). File is 0 Open..., 1 Export
; as BMP... - and THOSE ITEM NUMBERS ARE THE DIALOG MODES, FDLG_OPEN and
; FDLG_SAVE, so AL goes to OSAPI_FILE_DLG as it arrives (asserted below).
; -----------------------------------------------------------------------------
%if FDLG_OPEN != 0 || FDLG_SAVE != 1
    %error "ct_oncmd hands the File item number to OSAPI_FILE_DLG as its mode"
%endif
ct_oncmd:
    cmp ah, 1                           ; AH = the menu, AL = the item
    je .gallery
    cmp ah, 2
    je .data
    push bx
    push si
    push di
    mov bx, si
    mov di, ct_ondlg
    xor si, si                          ; no default name for Open
    or al, al
    jz .dlg                             ; AL = 0 = FDLG_OPEN
    mov si, ct_s_noexp
    cmp word [ct_valcnt], 0
    je .say
    mov di, ct_expdlg
    mov si, ct_s_chartbmp               ; AL = 1 = FDLG_SAVE
.dlg:
    call OSAPI_FILE_DLG
    jmp .dlgout
.say:
    call ct_toast
.dlgout:
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
    cmp byte [ct_name], 0
    je .ret                             ; nothing open: the choice is
    mov ax, ct_s_nocol                  ; remembered for the next Open
    jmp ct_load_show

; --- Gallery: pick a type and redraw what is already loaded -------------------
; The item index maps to CH_T_* through this table rather than by arithmetic,
; because the menu is in Excel's alphabetical order (Area, Bar, Column, Line)
; and CH_T_* is in the order the drawing code was written.
.gallery:
    push bx
    mov bl, al
    xor bh, bh
    mov al, [ct_gal_map + bx]           ; bytes: every CH_T_* is under 128,
    cbw                                 ; so CBW is the zero extension
    mov [ch_type], ax
    pop bx
    cmp word [ct_valcnt], 0
    je .ret                             ; nothing loaded: the type is still
    call ct_render                      ; remembered for the next Open
    call ct_paint
.ret:
    ret

; -----------------------------------------------------------------------------
; -----------------------------------------------------------------------------
; ct_about - the OSAPI_ABOUT_SET handler (slot 0x018A, SPEC.md 12.2). SI = our
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

; -----------------------------------------------------------------------------
; ct_toast - in: SI = NUL message; shows it as a menu-bar toast for the
; default ~3s (SPEC.md 59). Preserves all registers except flags: OSAPI_TOAST
; preserves every register but its outputs, and it has none (os88api.inc's
; contract for every slot), so only the two this loads are banked. The kernel
; COPIES the text (SPEC.md 59.3).
; -----------------------------------------------------------------------------
ct_toast:
    push cx
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
    ret

; -----------------------------------------------------------------------------
; ct_ondlg - the Open dialog's completion proc (SPEC.md 38.6). In: AL=mode
; (always 0, Open), SI=our window ptr, ES:DI=chosen name (ES=KERNEL_SEG); UI
; task, gfx lock HELD, dialog already destroyed - we owe the repaint. A newly
; opened file starts on Automatic, whatever the last one used.
; -----------------------------------------------------------------------------
ct_ondlg:
    push ax
    push cx
    push si
    push di
    mov si, di
    mov di, ct_name
    call ct_takename
    pop di
    pop si
    mov word [ct_wantcol], 0
    mov ax, ct_s_noval
    call ct_load_show
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_load_show - read [ct_name] under [ct_wantcol], render it and repaint, or
; say why not. Dispatches by extension into one of the three independent
; readers (ct_read_by_ext); zero values renders an empty white canvas, same as
; Sheet's own chart window with nothing charted yet, and says AX.
; in:  SI = our window ptr, AX = the toast for "read, and nothing to chart";
;      gfx lock held. Preserves every register but AX.
;
; BX holds the window across the read AND ct_render, both of which bank it -
; which is the whole of 82.10. It is the one tail of the two callers that used
; to carry a copy each (ct_ondlg's and Data > Column's ct_reread), differing
; only in the sentence for an empty result.
; -----------------------------------------------------------------------------
ct_load_show:
    push bx
    push si
    mov bx, si
    call ct_read_by_ext                 ; CF=1: SI = what went wrong
    jc .say
    call ct_render
    mov si, bx
    call ct_paint
    cmp word [ct_valcnt], 0
    jne .out
    mov si, ax
.say:
    call ct_toast
.out:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; ct_stage - read [ct_name] into a claim SIZED TO IT, held only until the
; caller's callback returns (SPEC.md 82.14).
; out: CF=0: ES = [ct_stgseg], the claim to give back, with the file's bytes
;            at ES:CT_FOFF and CX of them, and the series' arrays below
;      CF=1: SI = the toast to show; nothing is held
; clobbers AX, DX, SI
;
; THE PROBE IS A READ WITH NO ROOM. OSAPI_FILE_READ decides FERR_BIG from the
; directory entry BEFORE any data I/O, leaves the buffer alone, and answers
; DX = the KB the read needs - the UNPACKED size, so a compressed file is
; sized right without this code knowing it is one (os88api.inc,
; SPEC.md 20.14.6.3). An empty file answers CF=0 and DX = 0, and still gets
; the one KB the series live in. The second read is then one more walk of a directory
; the first just walked, and the hintless-file sniff it repeats is answered
; out of SPEC.md 18.95's track cache.
;
; CT_STG_MAX_KB is the fixed claim this replaced, kept as the ceiling: a file
; that needed more was FERR_BIG against it and "Could not read that file",
; and still is - the claim got smaller, the set of files Chart opens did not.
; -----------------------------------------------------------------------------
ct_stage:
    push bx
    push ds                             ; ES:BX = DS:0 with a capacity of 0:
    pop es                              ; the probe, which writes nothing
    xor bx, bx
    xor cx, cx
    xor dx, dx
    mov si, ct_name
    call OSAPI_FILE_READ
    jnc .claim                          ; an empty file: DX:AX = 0 bytes
    cmp ax, FERR_BIG
    jne .rerr
    cmp dx, CT_STG_MAX_KB
    ja .rerr
.claim:
    mov ax, dx                          ; AX = the KB the file needs, plus
    inc ax                              ; the one under CT_FOFF for the series
    call OSAPI_MEM_CLAIM                ; - and the claim leaves AX alone
    mov si, ct_s_nomem                  ; (every slot preserves all but its
    jc .out                             ; outputs)
    mov [ct_stgseg], dx
    mov es, dx
    dec ax
    mov ch, al                          ; CX = the file's KB * 1024: AX <= 32,
    xor cl, cl                          ; so it fits a word
    shl cx, 1
    shl cx, 1
    mov bx, CT_FOFF
    xor dx, dx
    mov si, ct_name
    call OSAPI_FILE_READ
    jnc .got
    mov dx, [ct_stgseg]
    call OSAPI_MEM_FREE
.rerr:
    mov si, ct_s_readerr
    stc
    jmp .out
.got:
    mov cx, ax                          ; a file this small never exceeds 64KB
.out:
    pop bx
    ret

; -----------------------------------------------------------------------------
; ct_read_by_ext - stage [ct_name] and run the reader its extension names, and
; hand the staging back. out: CF=0 read; CF=1 = it was not, SI = the toast.
; Every other register is preserved - BX in particular (every reader banks it,
; 82.10), so a caller may keep its window pointer there.
;
; The extension test is the last four characters against ".DIF" and ".BIF"
; (8.3 names arrive uppercase from the kernel, so it is case-sensitive), and
; a name shorter than four characters is SYLK, like anything else.
; -----------------------------------------------------------------------------
ct_read_by_ext:
    push ax
    push cx
    push dx
    push di
    push es
    call ct_stage                       ; ES:CT_FOFF = the file, CX bytes
    jc .out
    add cx, CT_FOFF                     ; CX = where the readers stop
    xor dx, dx                          ; both series start empty: ct_t2cnt
    mov [ct_tcnt], dx                   ; is written only inside ct_record, so
    mov [ct_t2cnt], dx                  ; zeroing just ct_tcnt carried the
                                        ; PREVIOUS file's second column into
                                        ; this one's chart. ct_mincol and
                                        ; ct_mincol2 need nothing: a count of
                                        ; zero is what ct_record tests first
    mov di, ct_name
.end:
    cmp byte [di], 0
    je .atend
    inc di
    jmp .end
.atend:
    mov ax, ct_read_sylk
    cmp di, ct_name + 4
    jb .go
    cmp word [di-2], 'IF'
    jne .go
    mov dx, [di-4]
    cmp dx, '.D'
    jne .notdif
    mov ax, ct_read_dif
.notdif:
    cmp dx, '.B'
    jne .go
    mov ax, ct_read_biff
.go:
    call ax
    mov dx, [ct_stgseg]                 ; ...and the staging goes back before
    call OSAPI_MEM_FREE                 ; the callback returns
    clc
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_expdlg - the Export dialog's completion proc. ES:DI = the name; gfx lock
; held.
;
; THE CANVAS ALREADY IS THE FILE (SPEC.md 82.1): the 118-byte header and
; palette at 0, the pixels after - except that BMP keeps its rows bottom-up and
; the canvas is top-down for OSAPI_GFX_BLIT4. ch_bmp_write answers that by
; staging a reordered copy in a second 19KB segment; this turns the canvas's
; own rows over, writes it in one OSAPI_FILE_WRITE and turns them back, so an
; export needs no claim and cannot fail for want of one. Nothing can paint the
; canvas in between: ct_paint runs on the UI task, which is this one, inside
; this callback. The file written is byte-identical to ch_bmp_write's.
; -----------------------------------------------------------------------------
ct_expdlg:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, di
    mov di, ct_ntxt                     ; NOT ct_name: that is the file being
    call ct_takename                    ; CHARTED, and Data > Column re-reads
    mov es, [ct_chartseg]               ; it. ct_ntxt is ct_esatof's scratch,
    call ct_flip                        ; dead outside a read
    xor bx, bx
    mov cx, CH_HDRSZ + (CH_STRIDE * CH_H)   ; 19318, fits one word
    xor dx, dx
    mov si, ct_ntxt
    call OSAPI_FILE_WRITE
    pushf
    call ct_flip                        ; back the right way up, written or not
    popf
    mov si, ct_s_exported
    jnc .say
    mov si, ct_s_experr
.say:
    call ct_toast
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; ct_flip - turn the canvas's CH_H pixel rows over in place; its own inverse.
; in: ES = [ct_chartseg]. Clobbers AX, CX, SI, DI.
; One word of each row pair per pass: AX takes the lower row's word, MOVSW
; puts the upper row's word over it, and AX goes where that came from.
; -----------------------------------------------------------------------------
ct_flip:
    push ds
    push es
    pop ds
    mov si, CH_PXOFF
    mov di, CH_PXOFF + (CH_H - 1) * CH_STRIDE
    cld
.row:
    mov cx, CH_STRIDE / 2
.w:
    mov ax, [di]
    movsw
    mov [si-2], ax
    loop .w
    sub di, 2 * CH_STRIDE
    cmp si, di
    jb .row
    pop ds
    ret

; -----------------------------------------------------------------------------
; ct_takename - the dialog's answer at ES:SI into DS:DI, NUL-terminated, at
; most CT_NAMEMAX characters (an 8.3 name; the buffer is one longer). Both
; dialog procs used to carry this loop; it is here once.
; Clobbers AX, CX, SI, DI.
; -----------------------------------------------------------------------------
ct_takename:
    mov cx, CT_NAMEMAX
.c:
    es lodsb
    mov [di], al
    inc di
    or al, al
    loopnz .c
    mov byte [di], 0
    ret

; -----------------------------------------------------------------------------
; ct_pint - parse a signed decimal integer
; in: ES:SI=ptr, BX=limit (exclusive, an offset); also stops at NUL
; out: AX=value, SI=advanced; BX preserved; ES must be set by the caller
; -----------------------------------------------------------------------------
ct_pint:
    push cx
    push dx
    xor cx, cx                          ; CL = 1: there was a minus sign
    xor ax, ax
    cmp si, bx
    jae .fin
    cmp byte [es:si], '-'
    jne .digits
    inc cx
    inc si
.digits:
    cmp si, bx
    jae .fin
    mov ch, [es:si]
    sub ch, '0'                         ; NUL and everything below '0' wrap
    cmp ch, 9                           ; past 9, so this one unsigned test is
    ja .fin                             ; the whole of "is it a digit"
    mov dx, ax                          ; AX * 10 as (AX*4 + AX) * 2: the same
    shl ax, 1                           ; low word MUL gave, and no register
    shl ax, 1                           ; to bank around it
    add ax, dx
    shl ax, 1
    add al, ch
    adc ah, 0
    inc si
    jmp .digits
.fin:
    test cl, cl
    jz .nosign
    neg ax
.nosign:
    pop dx
    pop cx
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
; ct_record - offer one cell to the series (the CT_TCAP fix)
; in:  AX = col, BX = row, and THE VALUE IN ch_dbl - eight bytes, not a word
; in DX. Stage 4.6: a cell holds an IEEE-754 double, and truncating it here
; was the whole of why 43.6 charted as a bar of 43 (82.13). All registers
; preserved.
;
; THE CAP USED TO BOUND THE SCAN, AND THAT LOST DATA SILENTLY. Each reader
; collected every numeric cell it met into the temp arrays, stopped at CT_TCAP
; of them, and only then did ct_finalize pick the lowest column and filter to
; it. On a wide sheet the temp arrays filled with OTHER columns' cells, so two
; things went wrong at once and neither announced itself: the tail of the
; chosen column was never read, and - worse - the lowest column was derived
; from a truncated sample, so a lower column appearing later in the file was
; never seen and THE WRONG COLUMN WAS CHARTED. Both produced a plausible chart.
;
; So the filter runs as the file is read instead. The lowest column seen so far
; is the series; a cell BELOW it restarts the collection, a cell IN it is
; appended, a cell ABOVE it is offered to the second series. One pass still,
; no second read, and the cap bounds the KEPT SERIES rather than the scanned
; candidates - which is why it is CH_MAXBARS here and not CT_TCAP.
;
; WHAT IS KEPT IS ONLY WHAT IS READ AGAIN (size pass 1): a row per cell of
; series one, because ct_finalize sorts by it, and the doubles of both. A
; column word per cell went (every cell of a series is in ct_mincol by
; construction), and so did series two's rows, which nothing ever read - it is
; drawn in the order the file gave it, exactly as before. And what is kept is
; kept in the STAGING claim, at ES:CT_A_* (ES is that claim for as long as a
; reader runs), not in the package's bss: none of it outlives the read.
; -----------------------------------------------------------------------------
ct_record:
    push cx
    push si
    push di
    mov cx, [ct_wantcol]                ; Data > Column: anything to the LEFT of
    jcxz .anycol                        ; the chosen column is not a candidate,
    dec cx                              ; so the chosen one becomes the lowest
    cmp ax, cx                          ; and the existing two-lowest logic
    jb .out                             ; picks the next one along as series 2.
.anycol:                                ; THE MENU IS 1-BASED AND AX IS NOT -
                                        ; ct_parse_c's .apply already did the
                                        ; dec - comparing the two directly
                                        ; charted the column after the one
                                        ; asked for
    mov cx, [ct_tcnt]
    jcxz .newcol                        ; nothing yet: this cell defines it
    cmp ax, [ct_mincol]
    je .append
    jb .newcol                          ; a LOWER column supersedes everything
    ; --- higher than the series: it may still be the SECOND one -------------
    ; Scatter and Combination need two (SPEC.md 82.8), so the next-lowest
    ; column is kept as well. The same three-way test, one level along.
    mov cx, [ct_t2cnt]
    jcxz .new2
    cmp ax, [ct_mincol2]
    ja .out
    je .append2
.new2:
    mov [ct_mincol2], ax
    xor cx, cx                          ; ...and series two starts again
.append2:                               ; CX = its count
    cmp cx, CH_MAXBARS
    jae .out
    mov di, cx
    inc cx
    mov [ct_t2cnt], cx
    mov cl, 3
    shl di, cl
    add di, CT_A_T2VAL                  ; ES:DI = series two's [old count]
    jmp .store
.newcol:                                ; CX = series one's count. The old
    jcxz .nodemote                      ; series becomes the second one rather
    push ax                             ; than being thrown away - it IS the
    mov ax, [ct_mincol]                 ; next-lowest column by construction
    mov [ct_mincol2], ax
    mov [ct_t2cnt], cx
    shl cx, 1                           ; four words a double
    shl cx, 1
    mov si, CT_A_TVAL
    mov di, CT_A_T2VAL
    push ds                             ; both arrays are in the staging claim
    push es
    pop ds
    cld
    rep movsw
    pop ds
    pop ax
.nodemote:
    mov [ct_mincol], ax
    xor cx, cx                          ; ...and series one starts again
.append:                                ; CX = its count
    cmp cx, CH_MAXBARS
    jae .out                            ; the series is full; a longer column
                                        ; is truncated, which CH_MAXBARS has
                                        ; always meant
    mov di, cx
    inc cx
    mov [ct_tcnt], cx
    shl di, 1
    mov [es:CT_A_TROW + di], bx
    shl di, 1
    shl di, 1
    add di, CT_A_TVAL                   ; ES:DI = series one's [old count]
.store:
    mov si, ch_dbl                      ; DS:SI -> ES:DI, the eight bytes
    cld
    movsw
    movsw
    movsw
    movsw
.out:
    pop di
    pop si
    pop cx
    ret

; -----------------------------------------------------------------------------
; ct_finalize - the shared last step for all three readers: sort series one by
; row ascending, IN PLACE, then scale both series' doubles into the words the
; drawing runs on, and copy those words - the only thing the read leaves
; behind - out of the staging claim into ct_w2vals/ct_wvals.
; in: ES = the staging claim. out: ES = DS. Clobbers DI.
;
; ct_record already keeps only the lowest column (and caps it at CH_MAXBARS),
; so all this has left to do is order it - and it orders the rows and doubles where
; they stand. It used to copy them into a second set of arrays (ct_vrow,
; ct_vals: 400 bytes) to sort the copy, which bought nothing: nothing reads
; the unsorted order afterwards. The sort is the same insertion sort, stable,
; so the order it produces is the same.
;
; A read that never reaches a reader (the file would not stage) never gets
; here, so the chart on screen and the words it was drawn from stay together.
; -----------------------------------------------------------------------------
ct_finalize:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, 1                           ; at most CH_MAXBARS=40 items, so an
.outer:                                 ; O(n^2) sort costs nothing that
    cmp cx, [ct_tcnt]                   ; matters here
    jae .sorted
    mov si, cx
    shl si, 1
.inner:
    or si, si
    jz .outernext
    mov ax, [es:CT_A_TROW + si]
    mov bx, [es:CT_A_TROW + si - 2]
    cmp ax, bx
    jae .outernext
    mov [es:CT_A_TROW + si], bx
    mov [es:CT_A_TROW + si - 2], ax
    push si                             ; and the eight bytes that belong with
    push cx                             ; the row, swapped the same way. The
    shl si, 1                           ; sort's OUTER index lives in CX -
    shl si, 1                           ; counting the four words in it reset
    add si, CT_A_TVAL                   ; the outer walk after every swap
    lea di, [si - 8]
    mov cx, 4
.swap8:
    mov ax, [es:si]
    xchg ax, [es:di]
    mov [es:si], ax
    inc si
    inc si
    inc di
    inc di
    loop .swap8
    pop cx
    pop si
    dec si
    dec si
    jmp .inner
.outernext:
    inc cx
    jmp .outer
.sorted:
    ; --- the doubles become the words the drawing runs on (82.13) ----------
    ; SERIES TWO FIRST, so [ch_e10] is left holding SERIES ONE's exponent -
    ; that is the one the value axis is labelled from, and the second series
    ; is drawn against its own ch_max2 with no scale of its own.
    mov dx, es                          ; ch_scale wants both arrays in DX
    mov si, CT_A_T2VAL
    mov di, CT_A_W
    mov cx, [ct_t2cnt]
    call ch_scale
    mov si, CT_A_TVAL
    mov di, CT_A_W + CH_MAXBARS*2
    mov cx, [ct_tcnt]
    call ch_scale
    mov si, CT_A_W                      ; ...and the words, both series in one
    mov di, ct_w2vals                   ; move: ct_wvals follows ct_w2vals as
    mov cx, CH_MAXBARS * 2              ; series one follows series two here.
    push ds                             ; Past a count the words are stale -
    push es                             ; and were before: ch_draw reads only
    pop ds                              ; the count
    pop es
    cld
    rep movsw
    push es
    pop ds
    pop si
    pop dx
    pop cx
    pop bx
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
; ct_read_biff - in: ES:CT_FOFF = the staged file, CX = where it ends;
; both series empty (ct_read_by_ext).
; Walks real [opcode:word][length:word] BIFF record headers; on an RK cell
; record (0x027E: row,col,xf,rk_lo,rk_hi, 10 bytes) decodes the value via
; ct_rkdec, on a NUMBER record (0x0203: row,col,xf and eight bytes of
; IEEE-754) takes the double verbatim, and offers (row,col,value) to
; ct_record, which keeps only the lowest column. Stops at EOF (0x000A) or
; a truncated trailing record.
;
; CX is the walk's end bound for the whole walk: the NUMBER path's eight-byte
; copy is four moves rather than a counted loop, so nothing here needs CX as
; a counter - which is why ct_biffend, the bound's banked copy, is gone.
; -----------------------------------------------------------------------------
ct_read_biff:
    push ax
    push bx
    push dx
    push si
    mov si, CT_FOFF
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
    jne .skip                           ; verbatim, and the ONLY way a value
                                        ; that is not an exact small integer
                                        ; reaches a BIFF file at all
    cmp dx, 14                          ; too short to hold row/col/xf plus
    jb .skip                            ; the eight bytes
    mov ax, si                          ; the whole record must be in the
    add ax, dx                          ; buffer, or the walk ends here
    jc .done                            ; a wrapped sum passes the compare
    cmp ax, cx
    ja .done
    mov ax, [es:si+6]                   ; past row/col/xf: the eight bytes
    mov [ch_dbl], ax
    mov ax, [es:si+8]
    mov [ch_dbl+2], ax
    mov ax, [es:si+10]
    mov [ch_dbl+4], ax
    mov ax, [es:si+12]
    mov [ch_dbl+6], ax
    jmp .cell
.isrk:
    cmp dx, 10                          ; too short to hold row/col/xf/rk:
    jb .skip                            ; stale buffer bytes are not a value
    mov ax, si                          ; the whole record must be in the
    add ax, dx                          ; buffer, or the walk ends here
    jc .done                            ; a wrapped sum passes the compare
    cmp ax, cx
    ja .done
    push dx                             ; length, saved across the decode
    mov ax, [es:si+6]                   ; rk lo
    mov dx, [es:si+8]                   ; rk hi
    call ct_rkdec                       ; -> ch_dbl
    pop dx
.cell:
    mov ax, [es:si+2]                   ; ax = col
    mov bx, [es:si]                     ; bx = row
    call ct_record
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
    jmp ct_finalize

; -----------------------------------------------------------------------------
; ct_read_sylk - in: ES:CT_FOFF = the staged file, CX = where it ends;
; both series empty (ct_read_by_ext).
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
    mov di, cx                          ; di = end offset
    mov si, CT_FOFF
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
    jmp ct_finalize

; -----------------------------------------------------------------------------
; ct_parse_c - the fields of one 'C' record; in: SI=tokens start (right
; after "C;"), BX=line end (exclusive); ES = the staged file, the same buffer
; ct_read_sylk is walking.
;
; The record's X, Y and "has a K" live in DX, CX and DI for the length of
; the record: ct_pint and ct_esatof (and the fp_atof/fp_pack_a under it)
; preserve all three, and the value itself stays in ch_dbl, which nothing
; between a K and .apply touches - so the four bss scratch cells this used
; to bank them in, eight bytes of them a copy of ch_dbl, are gone. The last
; K wins, as it always did.
; -----------------------------------------------------------------------------
ct_parse_c:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    xor dx, dx                          ; DX = X, the column (0 = none given)
    xor cx, cx                          ; CX = Y, the row (0 = none given)
    xor di, di                          ; DI = a K value is in ch_dbl
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
    mov dx, ax
    jmp .tok
.isy:
    inc si
    call ct_pint
    mov cx, ax
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
    inc di
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
    or di, di
    jz .out
    mov ax, dx
    cmp ax, 1
    jb .out
    cmp cx, 1
    jb .out
    dec ax                              ; 1-based -> 0-based
    dec cx
    mov bx, cx                          ; bx = row, ax = col
    call ct_record
.out:
    pop di
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
    lea bx, [si+3]                      ; SI < DI <= 33KB: this cannot wrap
    cmp bx, di
    ja .no
    cmp word [es:si], 'BO'
    jne .no
    cmp byte [es:si+2], 'T'
    jne .no
    cmp bx, di
    jae .yes
    mov al, [es:bx]
    cmp al, 13
    je .yes
    cmp al, 10
    jne .no
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
; ct_read_dif - in: ES:CT_FOFF = the staged file, CX = where it ends;
; both series empty (ct_read_by_ext).
; Skips the header STRUCTURALLY - unlike Sheet's own closed-loop DIF
; reader, which safely assumes its own writer's fixed 12-line header, this
; reads files it did not write, so it scans line by line for the first
; line that is exactly "BOT" (the real DIF row marker) rather than
; assuming any particular header length. From there, walks rows exactly
; like the grammar this project's own writer emits (each row: "-1,0" then
; "BOT"; each cell: "0,<value>" then "V", or anything else, meaning
; NA/blank). Offers (row,col,value) to ct_record, which keeps the lowest
; column and caps the series at CH_MAXBARS. The row and column counters are
; DX and CX for the whole walk (ct_esatof, ct_difskipline, ct_is_bot_line and
; ct_record all preserve both), and the value stays in ch_dbl from the number
; line to the V line - nothing between them touches it - so the two bss
; counters and the eight-byte round trip through a scratch copy are gone.
; -----------------------------------------------------------------------------
ct_read_dif:
    push ax                             ; CX is not banked: the length is
    push bx                             ; copied into DI and CX is the column,
    push dx                             ; and the one caller, ct_read_by_ext,
    push si                             ; banks it
    push di
    mov di, cx                          ; di = end offset
    mov si, CT_FOFF
.hdrscan:
    cmp si, di
    jae .done                           ; no BOT anywhere: no data
    call ct_is_bot_line
    jc .foundbot
    call ct_difskipline
    jmp .hdrscan
.foundbot:
    call ct_difskipline                 ; consume the first row's BOT line
    xor dx, dx                          ; DX = the row
    xor cx, cx                          ; CX = the column
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
    inc dx
    xor cx, cx
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
    call ct_difskipline                 ; finish the "0,<value>" line
    cmp si, di
    jae .cellnext
    cmp byte [es:si], 'V'               ; the real DIF value-indicator
    jne .notvalid
    mov bx, dx                          ; the value is still in ch_dbl
    mov ax, cx
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
    inc cx
    jmp .cellloop
.done:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    jmp ct_finalize

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

ct_gal_map:    db CH_T_AREA, CH_T_BAR, CH_T_COLUMN, CH_T_LINE, CH_T_PIE, CH_T_SCATTER, CH_T_COMBO
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
ct_s_readerr:  db 'Could not read that file', 0
ct_s_nomem:    db 'Not enough memory.', 0
ct_s_noval:    db 'No numeric data found.', 0

; stage: shared rasterizer + BMP writer - see that file's own header
; comment for the CH_* constants and ch_* bss words it requires, both
; declared above. Included here, just before OS88_BSS, for the same
; fixed-offset reason its own header states: code between the header and
; here would break the icon macro's fixed-offset assertion (this package
; has no icon, but the same %include-at-the-end rule still applies) and
; would move the entry point.
%define FP_NO_FTOA                     ; SPEC.md 84.8: what Chart never calls
%define FP_NO_SQRT                     ; is not assembled - no number is ever
%define FP_NO_FLOOR                    ; printed here, and no formula is
%define FP_NO_I2X                      ; evaluated. 943 bytes of image and
%define FP_NO_SUB                      ; 52 of the scratch below
%define CH_NO_BMPWRITE                 ; the export is ct_flip's, in place
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
    OS88_BSS 479
    OS88_IMAGE_END

ct_chartseg equ os88_image_end + 0  ; word: the offscreen canvas claim
ct_stgseg   equ ct_chartseg + 2     ; word: the file being read's staging
                                     ; claim - meaningful only inside
                                     ; ct_read_by_ext, which frees it before
                                     ; it returns (SPEC.md 82.14)
ct_name     equ ct_stgseg + 2       ; 13: the charted file's 8.3 name
ct_tcnt     equ ct_name + 13        ; word: how many cells series one holds -
ct_valcnt   equ ct_tcnt             ; ...which IS the count charted: every
                                     ; reader ends in ct_finalize, so outside
                                     ; a read the two cannot differ, and the
                                     ; separate word only ever copied this one
ct_mincol   equ ct_tcnt + 2         ; word: series one's column
ct_w2vals   equ ct_mincol + 2       ; CH_MAXBARS words: ch_scale's output for
                                     ; series two, and...
ct_wvals    equ ct_w2vals + CH_MAXBARS*2    ; ...for series one: the signed
                                     ; words the drawing reads, plus [ch_e10]
                                     ; to say what they mean. ONE move fills
                                     ; both (ct_finalize), so they stay in
                                     ; this order and adjacent. The rows and
                                     ; doubles they come from live in the
                                     ; staging claim (CT_A_*)
ct_ntxt     equ ct_wvals + CH_MAXBARS*2 ; CT_NTXT_MAX+1: ct_esatof's DS copy
                                     ; of one number out of the staged file -
                                     ; and ct_expdlg's export name, a moment
                                     ; when no read is running

; --- apps/os88chart.inc's own required scratch (see its header comment) -------
ch_max      equ ct_ntxt + CT_NTXT_MAX + 1
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
ct_t2cnt    equ ct_mincol2 + 2      ; ...and how many cells it has
ct_wantcol  equ ct_t2cnt + 2        ; word: 0 = chart the lowest
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
fp_sgn            equ fp_tmp + 2       ; fp_atof's sign. fp_dig/fp_d10/fp_nd
                                       ; (fp_ftoa's), fp_sq/fp_g (fp_sqrt's)
                                       ; and fp_tv (fp_floor's) are not here:
                                       ; the FP_NO_* gates above dropped the
                                       ; routines that name them
fp_hw             equ fp_sgn + 2       ; --- the coprocessor path ---
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
%if CT_NTXT_MAX < CT_NAMEMAX
    %error "ct_expdlg stages the export name in ct_ntxt, which is too short"
%endif
%define CT_BSS_NEED (ct_bss_end - os88_image_end)
    times (CT_BSS_NEED - OS88_BSS_SIZE) db 0
    times (OS88_BSS_SIZE - CT_BSS_NEED) db 0
