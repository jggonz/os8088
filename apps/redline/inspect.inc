; =============================================================================
; apps/redline/inspect.inc - REDLINE's Inspect tab (SPEC.md 103.7)
;
; The third of the window's tabs, drawn between Benchmark and Tune: a
; CheckIt-style survey of the machine. A strip of eight SUB-PAGE buttons sits
; under the tabs - Summary, CPU, Memory, Video, Drives, Ports, IRQ/DMA and
; Devices - and under it one scrollable pane, the Tune tab's shape: lines
; composed once, padded to their width and drawn opaque (SPEC.md 6.1), the
; shared scroll bar where they overflow. A line is one of three kinds:
;
;   RIA_TEXT    a label and its value, or a table row
;   RIA_HEAD    a section header, inverted across the pane (blue on colour)
;   RIA_BAR     a label, a proportion bar and its figure (memory, disk use)
;
; THE PROBE RUNS ONCE, on the first entry to the tab, and again on Refresh:
; everything a page shows is read into ri_* state there, so a sub-page change
; is a composition and a repaint of the pane and of two strip buttons, never
; a second probe. EVERYTHING HERE READS - SPEC.md 103.2's rule binds it
; whole: the only port WRITES are the ones a read cannot be made without,
; each named where it is made (a UART's scratch register and a latched
; divisor, the PIC's read-register select, the game port's one-shots).
;
; THE REPAINTS ARE ARMED (103.6's rule): every handler's drawing takes
; OSAPI_WM_CLIP_SET first, through ru_clip.
; =============================================================================

RI_NPAGE    equ 8                   ; sub-pages
RI_NBTN     equ 3                   ; the foot: Refresh, Save report, Quit
RI_LINES    equ 80                  ; lines a page may compose
RI_LW       equ 74                  ; bytes a line slot: 72 cells + NUL + 1
RI_COLS     equ 72
RI_SUBY     equ 22                  ; the sub-page strip, content-relative
RI_SUBP     equ 74                  ; ...its pitch
RI_PANEY    equ CS_PANEY            ; the pane's frame (ru_panelay's)
RI_TEXTX    equ CS_TEXTX            ; its text, on the 8px grid
RI_VALC     equ 26                  ; the value column of a label line
RI_BARC     equ 40                  ; a bar line: label cells 0..39,
RI_BARN     equ 24                  ; ...the bar over 40..63,
RI_FIGC     equ RI_BARC + RI_BARN   ; ...and its figure from 64

ri_lbuf     equ bl_out              ; ONE LINE of the module's, as the painter
                                    ; draws it: bl_out is idle whenever a page
                                    ; is painted - live only inside a save
                                    ; (rl_run's workloads overlay it too, and
                                    ; no Inspect page is painted during a run)
RIA_TEXT    equ 0
RIA_HEAD    equ 1
RIA_BAR     equ 2

ri_page:    db 0                    ; the sub-page shown
ri_ready:   db 0                    ; 1: probed once
ri_color:   db 0                    ; 1: a 16-colour display (ri_geom)
ri_nl:      dw 0                    ; lines composed on this page
ri_rname:   dw 0                    ; the report's name (bl_numname's)
ri_mseg:    dw 0                    ; the module's DATA segment, 0 = not here
ri_mfar:    dw 0, 0                 ; ...its entry, through its CODE selector

; WHAT THE RESIDENT HALF MEASURES FOR THE MODULE, which copies it whole on
; every probe: the clock (it times rl_mul, which is resident) and the volume
; this instance stands on (OSAPI_VOL_STAT answers for the CALLER)
ri_pf:
ri_pmhz:    dw 0, 0                 ; MHz x 100, 0 = not measured
ri_pmsrc:   db 0                    ; 0 none, 1 MUL timed, 2 TSC, 3 CPUID
ri_pcurv:   db 0                    ; the volume this instance stands on
ri_pvsok:   db 0                    ; 1: OSAPI_VOL_STAT answered
ri_pvspc:   dw 0                    ; ...sectors a cluster
ri_pvfree:  dw 0                    ; ...free clusters
ri_pvbps:   dw 0                    ; ...bytes a sector
ri_pvtot:   dw 0                    ; ...clusters
RI_PFSZ     equ $ - ri_pf

; The foot (cs_tabset copies these six over ru_buttonlabels)
ri_flabels: dw ri_b_ref, cs_b_rep, ru_quit, ri_b_ref, ri_b_ref, ri_b_ref
ri_actions: db 'rsqxxxbih'          ; the buttons' keys on this tab
            db '12345678'           ; ...and the sub-pages'
ri_b_ref:   db 'Refresh', 0
ri_p0:      db 'Summary', 0
ri_p1:      db 'CPU', 0
ri_p2:      db 'Memory', 0
ri_p3:      db 'Video', 0
ri_p4:      db 'Drives', 0
ri_p5:      db 'Ports', 0
ri_p6:      db 'IRQ/DMA', 0
ri_p7:      db 'Devices', 0
ri_s_nomod: db 'Inspect module not read', 0

; The pane: ui.inc's scroll-pane record (SP_*)
ri_pane:    dw 0, 0, 0, ri_panepaint
            times 7 dw 0
            dw 0, 0, 0, 0

; =============================================================================
; Entering the tab (cs_tabset): the probe once, then the page. The pages are
; the MODULE's (inspmod.inc), a lazy part of REDLINE.O88 fetched here the
; first time and kept; everything below draws what it composed
; =============================================================================
ri_enter:
    cmp byte [ri_ready], 0
    jne .comp
    call ri_probe
    mov byte [ri_ready], 1
.comp:
    mov word [ri_pane + SP_TOP], 0
    jmp ri_compose

; ri_probe - the resident's measurements, then the module's probe
ri_probe:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    call OSAPI_CUR_BUSY
    xor ax, ax                      ; the benchmark's own inventory, whose
    mov di, rl_ramtotal             ; E820 pass ACCUMULATES: from nothing
    mov [di], ax
    mov [di + 2], ax
    mov [di + 4], ax
    mov [di + 6], ax
    mov [rl_ramkb], ax
    mov [rl_ramkb + 2], ax
    mov [rl_mapok], al
    mov [rl_extok], al
    mov byte [rl_early], 0xFF
    call rl_inventory
    call OSAPI_CPU_INFO
    mov [cs_tier], al
    call cs_btscan                  ; the BIOS's copyright text (chipset.inc)
    call ri_clock
    call OSAPI_FILE_HERE            ; the volume this instance stands on: the
    mov [ri_pcurv], bl              ; one VOL_STAT answers for
    mov byte [ri_pvsok], 0
    call OSAPI_VOL_STAT
    jc .mod
    mov [ri_pvspc], ax
    mov [ri_pvfree], bx
    mov [ri_pvbps], cx
    mov [ri_pvtot], dx
    mov byte [ri_pvsok], 1
.mod:
    mov ax, RIM_PROBE
    call ri_mcall
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_clock - [ri_pmhz]: CPUID's nominal clock, the TSC's, or 64 MULs timed
; against the book cycles (rl_estimate's arithmetic), and nothing for the
; NEC, the 186 or a part the books do not cover
ri_clock:
    xor ax, ax
    mov [ri_pmhz], ax
    mov [ri_pmhz + 2], ax
    mov [ri_pmsrc], al
    mov ax, [rl_nominal]
    or ax, ax
    jz .tsc
    mov cx, 100
    mul cx
    mov bl, 3
    jmp short .set
.tsc:
    mov ax, [rl_tscmhz]
    mov dx, [rl_tscmhz + 2]
    mov bl, 2
    mov cx, ax
    or cx, dx
    jnz .set
    mov bx, 129                     ; rl_mulbook's: 808x / 286 / 386
    cmp byte [rl_early], 1
    jbe .time
    mov bx, 23
    cmp word [rl_cpuname], rl_cpu286
    je .time
    mov bx, 24
    cmp word [rl_cpuname], rl_cpu386
    jne .out
.time:
    push bx
    mov word [bl_n], 64
    mov word [bl_body], cs_noop
    call bl_time                    ; DX:AX = the passes' own cost
    mov si, ax
    mov di, dx
    mov word [bl_body], rl_mul
    call bl_time                    ; DX:AX = 64 passes of 64 MULs
    sub ax, si
    sbb dx, di
    pop bx
    jc .out
    push bx
    mov cx, 64
    call bl_div32                   ; counts a pass (clobbers BX)
    pop bx
    mov cx, ax
    jcxz .out
    mov ax, 7636                    ; 64 x 1.193182 x 100
    mul bx
    cmp dx, cx
    jae .out
    div cx
    xor dx, dx
    mov bl, 1
.set:
    mov [ri_pmhz], ax
    mov [ri_pmhz + 2], dx
    mov [ri_pmsrc], bl
.out:
    ret

; ri_compose - the page [ri_page], composed by the module; the pane's count
; follows. Without the module the page is its one line saying so
ri_compose:
    push ax
    mov word [ri_nl], 0
    mov al, RIM_COMPOSE
    mov ah, [ri_page]
    call ri_mcall
    jc .cnt
    mov [ri_nl], ax
.cnt:
    mov ax, [ri_nl]
    or ax, ax
    jnz .set
    inc ax
.set:
    mov [ri_pane + SP_CNT], ax
    pop ax
    ret

; =============================================================================
; THE MODULE (SPEC.md 20.12): fetched, stamped, PINNED while it is stood in
; and movable otherwise - pixel.asm's px_pcall, for one part and one task
; =============================================================================
; ri_mcall - AL = a verb, AH = its argument: the module's answer (CF, AX),
; or CF = 1 when it could not be read (op_fetch has said why, once).
; Every other register preserved
ri_mcall:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    push ax
    call ri_mfetch
    jc .no
    mov al, RI_MPART
    call op_seg
    or ax, ax
    jz .no
    mov es, ax
    mov [ri_mseg], ax
    cmp word [es:RIM_SIG], RIM_SIGVAL
    jne .no
    mov [es:RIM_SELF], ax           ; its own data segment, stamped each call
    mov ax, [es:RIM_ENTRY]
    mov [ri_mfar], ax
    mov al, RI_MPART
    call op_cseg                    ; a far call through the DATA segment is
    mov [ri_mfar + 2], ax           ; a #GP on kern_mega
    mov al, RI_MPART
    call op_pin                     ; nothing may move it while we are in it
    pop ax
    push ds
    pop es                          ; ES = ours, which the module reads
    call far [ri_mfar]              ; CS = the module, DS = ours
    pushf
    push ax
    mov al, RI_MPART
    call op_movable                 ; ...and it may move again (ri_mreloc)
    pop ax
    popf
    jmp short .out
.no:
    pop ax
    stc
.out:
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; ri_mfetch - the module here, read out of REDLINE.O88 (op_fetch reads by
; name in the instance's folder). REDLINE never moves its instance - it has
; no file dialog and no GOTO - so that folder is the one it was launched
; from, and PiXEL's home-and-back bracket would be bytes for nothing. CF = 1
; not to be had (op_fetch has said why)
ri_mfetch:
    mov al, RI_MPART
    call op_seg
    or ax, ax
    jz .fetch
    clc
    ret
.fetch:
    mov al, RI_MPART
    jmp op_fetch

; ri_mreloc - OP_ONMOVE (SPEC.md 20.12.12): the module moved, BX = was, DX
; = now. [ri_mseg] is the one word banked; ri_mcall rebuilds the far vector
; from op_cseg at every call, so nothing else names it
ri_mreloc:
    mov [ri_mseg], dx
    ret

; ri_line - DI = a line -> ES:SI = it in the module, AL = its kind; past
; the page, or with no module, ES:SI = ri_blanks in ours and AL = RIA_TEXT
ri_line:
    push dx
    push ds
    pop es
    mov si, ri_lbuf                 ; past the page: a blank line, made here
    push cx
    mov cx, RI_COLS
.b:
    mov byte [si], ' '
    inc si
    loop .b
    mov [si], ch
    pop cx
    mov si, ri_lbuf
    mov al, RIA_TEXT
    cmp di, [ri_nl]
    jae .out
    cmp di, RI_LINES
    jae .out
    mov ax, [ri_mseg]
    or ax, ax
    jz .none
    mov es, ax
    mov ax, RI_LW
    mul di
    add ax, RIM_LINES
    mov si, ax
    mov al, [es:RIM_ATTR + di]
    jmp short .out
.none:
    mov si, ri_s_nomod              ; the one line there is without it
    mov al, RIA_TEXT
.out:
    pop dx
    ret

; =============================================================================
; Geometry: inside ru_geometry, after the tabs' rects. The foot's three
; buttons (the other three parked where no press reaches), the sub-page
; strip and the pane
; =============================================================================
ri_geom:
    push ax
    push bx
    push cx
    push dx
    push di
    mov cx, RI_NBTN
    call ru_footlay                 ; the foot, Tune's way
    mov di, ru_rects + 9 * 8        ; past the three tabs: the strip
    mov ax, [ru_x]
    add ax, 8
    mov bx, [ru_y]
    add bx, RI_SUBY
    mov cx, RI_NPAGE
.sub:
    mov [di], ax
    mov [di + 2], bx
    mov dx, ax
    add dx, RI_SUBP - 5
    mov [di + 4], dx
    mov dx, bx
    add dx, 15
    mov [di + 6], dx
    add di, 8
    add ax, RI_SUBP
    loop .sub
    mov bx, ri_pane                 ; the pane: Tune's geometry, its cap ours
    mov ax, RI_LINES
    call ru_panelay
    push si                         ; colour headers and bars where the
    mov bx, [rl_win]                ; window's display has sixteen - and SI
    call OSAPI_WM_DISPLAY           ; is an OUTPUT of this slot, while every
    pop si                          ; press reaches here holding the window in
    mov byte [ri_color], 0          ; it (os88ui_btnclick)
    cmp dh, 4
    jb .mono
    mov byte [ri_color], 1
.mono:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_flags - the foot and the strip's state, for ru_buttons_paint
ri_flags:
    push ax
    push bx
    push cx
    push di
    push es
    push ds
    pop es
    cld
    xor ax, ax
    mov di, ru_buttonflags
    mov cx, 6
    rep stosw
    mov di, ru_buttonflags + 9 * 2
    mov cx, RI_NPAGE
    rep stosw
    mov bl, [ri_page]
    xor bh, bh
    shl bx, 1
    mov word [ru_buttonflags + 9 * 2 + bx], OS88UI_LATCH
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Painting
; =============================================================================
; ri_tabpaint - the tab's content, inside ru_paint (the ground is fresh)
ri_tabpaint:
    push ax
    push bx
    push cx
    push dx
    mov ax, 8
    mov bx, RI_PANEY
    mov cx, [ru_w]
    sub cx, 8
    mov dx, [ru_foot]
    sub dx, 4
    call ru_frame
    mov bx, ri_pane
    call ri_panepaint
    call ru_spbar
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_panepaint - BX = the pane: EVERY slot it shows, the lines past the
; page's last drawn as blanks, so a page change needs no erase pass
ri_panepaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx
    mov di, [ds:bp + SP_TOP]
    mov cx, [ds:bp + SP_VIS]
    mov dx, [ds:bp + SP_Y]
.line:
    jcxz .out
    call ri_drawline
    add dx, [ds:bp + SP_PITCH]
    inc di
    dec cx
    jmp short .line
.out:
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_drawline - DI = a line, DX = its relative y, BP = the pane. One opaque
; run across the pane's cells (a bar line: two runs and the bar between)
ri_drawline:
    push ax
    push bx
    push cx
    push si
    push es
    call ri_line                    ; ES:SI = the line, AL = its kind
    mov bx, [ds:bp + SP_COLS]
    cmp al, RIA_BAR
    jne .one
    cmp bx, RI_COLS                 ; a pane too narrow for the bar: its
    jb .text                        ; text alone (the bar cells are blank)
    call ri_drawbar
    jmp short .out
.one:
    cmp al, RIA_HEAD
    jne .text
    mov ax, CBLACK << 8 | CWHITE    ; AL = white ink, AH = the ground:
    cmp byte [ri_color], 0          ; black, or blue on sixteen colours
    je .run
    mov ah, CBLUE
    jmp short .run
.text:
    mov ax, CWHITE << 8 | CBLACK    ; AL = black ink, AH = white ground
.run:
    mov cx, RI_TEXTX
    call ri_cutrun
.out:
    pop es
    pop si
    pop cx
    pop bx
    pop ax
    ret

; ri_drawbar - SI = a bar line, DI = its index, DX = its relative y: the
; label's cells, the bar over RI_BARN cells (frame, ink, white - three
; disjoint passes, so no pixel is written twice), then the figure's cells
ri_drawbar:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov ax, CWHITE << 8 | CBLACK
    mov bx, RI_BARC
    mov cx, RI_TEXTX
    call ri_cutrun
    push si
    add si, RI_FIGC
    mov bx, RI_COLS - RI_FIGC
    mov cx, RI_TEXTX + RI_FIGC * 8
    call ri_cutrun
    pop si
    shl di, 1
    mov si, [es:RIM_LBAR + di]      ; SI = the fill, per mille
    mov ax, RI_TEXTX + RI_BARC * 8  ; the frame, inside the cells' 8 rows
    mov bx, dx
    mov cx, RI_TEXTX + RI_FIGC * 8 - 1 ; through the bar's last cell
    add dx, 7
    push ax
    push bx
    push cx
    push dx
    call ru_frame                   ; (it makes them absolute; black pen)
    pop dx
    pop cx
    pop bx
    pop ax
    push dx
    mov ax, RI_BARN * 8 - 2         ; interior width
    mul si
    mov cx, 1000
    div cx                          ; AX = filled pixels
    pop dx
    mov cx, ax
    mov ax, RI_TEXTX + RI_BARC * 8 + 1
    add ax, [ru_x]
    inc bx
    add bx, [ru_y]
    dec dx
    add dx, [ru_y]
    jcxz .rest
    push cx
    add cx, ax
    dec cx
    push ax
    mov al, CBLACK
    cmp byte [ri_color], 0
    je .ink
    mov al, CGREEN
.ink:
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    pop cx
    add ax, cx
.rest:
    mov cx, RI_TEXTX + RI_FIGC * 8 - 2
    add cx, [ru_x]
    cmp ax, cx
    jg .done
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
.done:
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; ri_cutrun - ES:SI = a line, BX = its cells, CX/DX = relative x/y, AL/AH
; = ink/ground: those cells copied into ri_lbuf (the line is the module's,
; and FONT_RUN reads OUR segment) and drawn as one opaque run
ri_cutrun:
    push bx
    push cx
    push dx
    push si
    push di
    push ax
    mov di, ri_lbuf
.c:
    or bx, bx
    jz .e
    mov al, [es:si]
    or al, al
    jz .e
    mov [di], al
    inc si
    inc di
    dec bx
    jmp short .c
.e:
    mov byte [di], 0
    pop ax
    push ax
    mov si, ri_lbuf
    add cx, [ru_x]
    add dx, [ru_y]
    call OSAPI_FONT_RUN
    pop ax
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret

; ri_redraw - the pane again from a handler, CLIPPED; the bar where it
; overflows, and its ground white where an earlier page had one and this
; page does not. AL = 1: the page changed (the strip's two buttons too)
ri_redraw:
    push ax
    push bx
    push cx
    push dx
    call ru_clip
    jc .out
    call ru_geometry
    mov bx, ri_pane
    call ri_panepaint
    call ru_spover
    jnc .nobar
    call ru_spbar
    jmp short .strip
.nobar:
    push ax
    mov ax, [bx + SP_SB]
    mov cx, [bx + SP_SB + 4]
    mov dx, [bx + SP_SB + 6]
    dec dx                          ; the frame's own rules stay
    mov bx, [bx + SP_SB + 2]
    inc bx
    dec cx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call OSAPI_GFX_FILL
    mov al, CBLACK
    call OSAPI_SET_COLOR
    pop ax
.strip:
    or al, al
    jz .out
    call ri_flags
    mov al, [ri_oldpg]
    add al, 10                      ; the strip's buttons are 10..17
    call ru_btn1
    mov al, [ri_page]
    add al, 10
    call ru_btn1
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
ri_oldpg:   db 0

; ri_goto - AL = a sub-page: compose it and show it from its top
ri_goto:
    cmp al, RI_NPAGE
    jae .out
    mov ah, [ri_page]
    mov [ri_oldpg], ah
    mov [ri_page], al
    mov word [ri_pane + SP_TOP], 0
    call ri_compose
    mov al, 1
    call ri_redraw
.out:
    ret

; =============================================================================
; Keys: rl_key's tail on this tab. AL = the key, AH = its scan code, DL =
; the letter upper-cased, and rl_key's eight registers pushed
; =============================================================================
ri_tabkey:
    cmp dl, 'R'
    je .refresh
    cmp dl, 'S'
    je .report
    cmp dl, 'Q'
    je .quit
    cmp al, '1'
    jb .nav
    cmp al, '1' + RI_NPAGE
    jae .nav
    sub al, '1'
    call ri_goto
    jmp .out
.nav:
    mov cx, ax                      ; CL = ascii, CH = scan code
    cmp ch, 0x4B                    ; Left / Right: the previous / next page
    je .left
    cmp ch, 0x4D
    je .right
    cmp cl, 9                       ; Tab: the next page, as Benchmark's views
    je .right
    mov bx, ri_pane                 ; the rest scroll the pane
    call ru_spkey
    jmp short .out
.left:
    mov al, [ri_page]
    dec al
    jns .pg
    mov al, RI_NPAGE - 1
    jmp short .pg
.right:
    mov al, [ri_page]
    inc al
    cmp al, RI_NPAGE
    jb .pg
    xor al, al
.pg:
    call ri_goto
    jmp short .out
.refresh:
    call ri_probe
    call ri_compose
    xor al, al
    call ri_redraw
    jmp short .out
.report:
    call ri_report
    mov si, ri_s_rptno
    jc .toast
    mov si, [ri_rname]
.toast:
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    jmp short .out
.quit:
    mov bx, [rl_win]
    call OSAPI_WM_CLOSE
.out:
    jmp ru_pop8

; ri_report - every page, in order, into INSPECTn.TXT (bl_numname's first
; free), through os88wseq.inc's stream: an append of any size, buffered in
; bl_out (free outside a save) and written in whole clusters. The module
; turns each page into text in place, in its own lines, and the stream takes
; it from there. The page shown is composed again after. CF = 1 not written
ri_report:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    mov al, [ri_page]
    push ax
    mov si, ri_s_file
    call bl_numname
    mov [ri_rname], si
    mov di, ri_ws
    mov bx, bl_out
    mov cx, 8192
    call os88_wseq_open
    jc .done
    xor cx, cx                      ; CL = the page
.page:
    mov al, RIM_TEXT
    mov ah, cl
    call ri_mcall                   ; AX = its text's bytes, in the module
    jc .done
    push cx
    mov cx, ax
    mov es, [ri_mseg]
    mov si, RIM_LINES
    call os88_wseq_put
    pop cx
    jc .done
    inc cx
    cmp cl, RI_NPAGE
    jb .page
    call os88_wseq_close
.done:
    pop ax
    pushf
    mov [ri_page], al
    call ri_compose
    popf
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
ri_ws:      times WSEQ_SIZE db 0    ; the report's stream (os88wseq.inc)
TOASTSTR ri_s_file, 'INSPECT.TXT'
TOASTSTR ri_s_rptno, 'Report not saved'

