; =============================================================================
; apps/redline/chipset.inc - REDLINE's Tune tab (SPEC.md 103.6)
;
; The second of the window's tabs (Benchmark, Tune), drawn into REDLINE's own
; window: no second window, no close box of its own, and the same foot of
; buttons the benchmark has - relabelled. It identifies the chipset (csrows.inc,
; read-only), measures an 8-bit read in ns a byte, and on a row that can write
; - the FE3600 today (csfe.inc) - offers the three writes a user needs:
;
;   Tune          sets the bus to the TARGET drop-down's choice:
;                   Fastest tested - the verified sweep, the fastest that
;                     read right SET;
;                   Mfg spec   - the data sheet's value
;   Save setting  REDLINE.CFG on the boot disk, which RLAPPLY puts back
;
; plus Save report and Quit. A framed field right of the two drop-downs
; says what the bus is running at NOW - Stock, Tuned or Mfg spec - so the
; state is never something to work out from the page. EVERY ACTION IS A
; BUTTON and every key is a button's (the owner's rule for a page that
; goes to strangers' machines); the report is a scrollable pane, and what
; each button does is written in it. There is no Measure: entering the tab and every write measure.
;
; THE REPAINTS ARE ARMED. A handler's drawing is not clipped by the kernel
; (W_PAINT is), so every incremental redraw here arms OSAPI_WM_CLIP_SET
; first - its absence once painted the pane straight over the alert the
; same handler had just opened, and the alert vanished unread (103.6).
; =============================================================================

CS_LINES    equ 39                  ; report lines composed
CS_LW       equ 74                  ; bytes a line slot: 72 cells + NUL + 1
CS_COLS     equ 72
CS_HELP0    equ 10                  ; the help lines: on the page, not in a
CS_HELPN    equ 8                   ; report (cs_report skips them)
CS_XTRA0    equ CS_HELP0 + CS_HELPN + CS_NROWS  ; then the field data:
CS_XTRAN    equ 5 + CS_SWMAX        ; BIOS text x2, registers, raw, sweep
CS_BTW      equ 66                  ; a BIOS string's slot: 65 + NUL
CS_DROPX    equ 80                  ; the drop-down, content-relative
CS_DROPY    equ 24
CS_PANEY    equ 42                  ; the pane's frame
CS_TEXTX    equ 16                  ; its text, on the 8px grid
CS_TGTX     equ 248                 ; "Target:" and its drop-down
CS_TGTDX    equ 304
CS_STATX    equ 448                 ; the state field's frame...
CS_STATW    equ 19                  ; ...and its cells
CS_NBTN     equ 4
CS_BTNW     equ 112
CS_NROMS    equ 6
CS_NOMEAS   equ 0xFFFF              ; a measurement not taken
CS_ROMSLOW  equ 300                 ; ns over RAM: an option ROM on the 8-bit
                                    ; bus, not a shadowed copy

cs_tab:     db 0                    ; the tab: 0 Benchmark, 1 Tune
cs_ready:   db 0                    ; 1: identified and measured once
cs_pick:    db 0                    ; the row the user has (the drop-down)
cs_conf:    db 0                    ; pick + 1 the user confirmed writing to
cs_warn:    db 0                    ; pick + 1 the user let sweep, untested
cs_pask:    db 0                    ; the alert up: 0 not detected, 1 untested
cs_pend:    db 0                    ; the key that asked, while the alert is up
cs_setv:    db 0FFh                 ; the R4 set this session, 0FFh = none
cs_meth:    db 0                    ; REDLINE.CFG's method for it
cs_saved:   db 0                    ; 1: REDLINE.CFG holds it
cs_tfast:   db 0                    ; Tune: the fastest that passed
cs_target:  db 0                    ; 0 Fastest tested, 1 Mfg spec
cs_stat:    times CS_STATW + 1 db 0 ; the state field, composed
cs_act:     db 0                    ; line 4: 0 a hint, 1 the tune, 2 cs_amsg
cs_amsg:    dw 0
cs_rname:   dw 0                    ; the report's file name (bl_numname's)
cs_measd:   db 0
cs_mhz:     dw 0                    ; the CPU clock, MHz x 100 (cs_clock)
cs_rd8:     dw CS_NOMEAS            ; the chipset's 8-bit read, ns over RAM
cs_tq:      dw CS_NOMEAS            ; ...in tenths of a CPU clock
cs_nsio:    dw CS_NOMEAS            ; an 8-bit I/O read (CS_IOPORT), ns over a NOP
cs_tqio:    dw CS_NOMEAS            ; ...in tenths of a CPU clock
cs_ion:     dw 0                    ; cs_iotime's PIT counts: the NOPs,
cs_ioi:     dw 0                    ; ...and the INs
cs_rawe:    dw 0                    ; the report's raw counts (cs_clock):
cs_rawm:    dw 0                    ; the empty pass, the MUL pass,
cs_rawn:    dw 0                    ; ...the I/O NOPs and INs
cs_rawi:    dw 0
cs_btdone:  db 0                    ; cs_bt is scanned
CS_IOPORT   equ 0x3F4               ; the floppy controller's main status
cs_nsscr:   dw CS_NOMEAS            ; ns a byte over RAM: the screen,
cs_nspm:    dw CS_NOMEAS            ; ...a PicoMEM's buffer
cs_pmseg:   dw 0
cs_nroms:   dw 0
cs_roms:    times CS_NROMS * 3 dw 0 ; segment, KB, ns
cs_tseg:    dw 0                    ; the read body's source
cs_toff:    dw 0
cs_ram:     dw 0                    ; the RAM twin's counts
cs_lcur:    dw 0                    ; the line being composed: its write ptr
cs_lend:    dw 0                    ; ...and its last cell

; --- the controls -------------------------------------------------------------
; The foot: REDLINE's own button record (ui.inc), relabelled by cs_tabset
cs_tlabels: dw cs_b_tune, cs_b_keep, cs_b_rep, ru_quit, cs_b_tune, cs_b_tune
cs_actions: db 'tksqxxbh'           ; the eight buttons' keys on this tab
cs_b_tune:  db 'Tune', 0
cs_b_keep:  db 'Save setting', 0
cs_b_rep:   db 'Save report', 0

; The report pane: ui.inc's scroll-pane record (SP_*)
cs_pane:    dw 0, 0, CS_LINES, cs_panepaint
            times 7 dw 0
            dw 0, 0, 0, 0

cs_items:   dw cs_n_none, cs_n_fe3600, cs_n_compaq, cs_n_scamp, cs_n_headland
            dw cs_n_scat, cs_n_scatsx, cs_n_neat, cs_n_wd76, cs_n_opti283
cs_drop:    dw 0, 0, 0, 0           ; OS88UI_DR_RECT, filled by cs_geom
            dw cs_items             ; OS88UI_DR_ITEMS
            dw CS_NROWS             ; OS88UI_DR_N
            dw 0                    ; OS88UI_DR_SEL
            dw 0                    ; OS88UI_DR_WIN
            db 0, 0FFh              ; OS88UI_DR_OPEN, OS88UI_DR_HOT
            dw 0, 0                 ; OS88UI_DR_SEG, OS88UI_DR_KB
            dw 0                    ; OS88UI_DR_TOP
            dw 0                    ; OS88UI_DR_DIS
%if ($ - cs_drop) != OS88UI_DR_SIZE
    %error cs_drop is not OS88UI_DR_SIZE bytes
%endif
cs_s_pick:  db 'Chipset:', 0

; The TARGET: what Tune sets the bus to. Live only where Tune is
cs_titems:  dw cs_t_fast, cs_t_spec
cs_tgt:     dw 0, 0, 0, 0           ; OS88UI_DR_RECT, filled by cs_geom
            dw cs_titems            ; OS88UI_DR_ITEMS
            dw 2                    ; OS88UI_DR_N
            dw 0                    ; OS88UI_DR_SEL
            dw 0                    ; OS88UI_DR_WIN
            db 0, 0FFh              ; OS88UI_DR_OPEN, OS88UI_DR_HOT
            dw 0, 0                 ; OS88UI_DR_SEG, OS88UI_DR_KB
            dw 0                    ; OS88UI_DR_TOP
            dw 0                    ; OS88UI_DR_DIS
%if ($ - cs_tgt) != OS88UI_DR_SIZE
    %error cs_tgt is not OS88UI_DR_SIZE bytes
%endif
cs_s_tgt:   db 'Target:', 0
cs_t_fast:  db 'Fastest tested', 0
cs_t_spec:  db 'Mfg spec', 0

; =============================================================================
; The tabs. cs_tabset changes the state and draws nothing; cs_settab is it
; and the repaint, when the tab changed. The first entry into Tune identifies
; and measures (a few milliseconds: no button needed for either).
; =============================================================================
cs_settab:                          ; AL = the tab
    push ax
    mov ah, [cs_tab]
    call cs_tabset
    cmp ah, [cs_tab]
    pop ax
    je .same
    jmp rl_repaint
.same:
    ret

cs_tabset:                          ; AL = the tab
    push ax
    push bx
    push cx
    push si
    push di
    push es
    cmp al, [cs_tab]
    je .out
    mov [cs_tab], al
    push ds
    pop es
    cld
    mov si, ru_benchlabels
    or al, al
    jz .labels
    mov si, cs_tlabels
.labels:
    mov di, ru_buttonlabels
    mov cx, 6
    rep movsw
    xor ax, ax                      ; the six foot flags: each tab sets its own
    mov di, ru_buttonflags
    mov cx, 6
    rep stosw
    call cs_drclose                 ; an open list goes with the tab (the whole
    cmp byte [cs_tab], 0            ; window repaints after, so CF is moot)
    je .out
    mov ax, [rl_win]
    mov [cs_drop + OS88UI_DR_WIN], ax
    mov [cs_tgt + OS88UI_DR_WIN], ax
    cmp byte [cs_ready], 0
    jne .comp
    mov byte [cs_ready], 1
    call cs_identify
    mov al, [cs_found]
    mov [cs_pick], al
    xor ah, ah
    mov [cs_drop + OS88UI_DR_SEL], ax
    call cs_measure
.comp:
    call cs_compose
.out:
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Geometry and painting. cs_geom runs inside ru_geometry, after the tabs'
; rects: the foot's four buttons (the other two are put where no press
; reaches), the two drop-downs and the pane.
; =============================================================================
cs_geom:
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, ru_rects
    mov ax, [ru_x]
    add ax, 8
    mov bx, [ru_y]
    add bx, [ru_foot]
    add bx, 3
    mov cx, CS_NBTN
.btn:
    mov [di], ax
    mov [di + 2], bx
    mov dx, ax
    add dx, CS_BTNW - 1
    mov [di + 4], dx
    mov dx, bx
    add dx, 17
    mov [di + 6], dx
    add di, 8
    add ax, CS_BTNW + 6
    loop .btn
    mov cx, 6 - CS_NBTN
.park:
    mov ax, 0x7FFF                  ; x1 > x2: os88ui_bhit never matches it
    mov [di], ax
    mov [di + 2], ax
    xor ax, ax
    mov [di + 4], ax
    mov [di + 6], ax
    add di, 8
    loop .park
    mov ax, [ru_x]                  ; the Target drop-down
    add ax, CS_TGTDX
    mov [cs_tgt + 0], ax
    add ax, 15 * 8 + OS88UI_DRAW + 4
    mov [cs_tgt + 4], ax
    mov ax, [ru_y]
    add ax, CS_DROPY
    mov [cs_tgt + 2], ax
    add ax, 12
    mov [cs_tgt + 6], ax
    mov ax, [ru_x]                  ; the Chipset drop-down
    add ax, CS_DROPX
    mov [cs_drop + 0], ax
    add ax, 17 * 8 + OS88UI_DRAW + 4
    mov [cs_drop + 4], ax
    mov ax, [ru_y]
    add ax, CS_DROPY
    mov [cs_drop + 2], ax
    add ax, 12
    mov [cs_drop + 6], ax
    mov bx, cs_pane                 ; the pane: its bar on the frame's right,
    mov ax, [ru_y]                  ; as Redline's other panes have it
    add ax, CS_PANEY
    mov [bx + SP_SB + 2], ax
    mov ax, [ru_foot]
    sub ax, 4
    mov dx, ax                      ; DX = the frame's bottom, relative
    add ax, [ru_y]
    mov [bx + SP_SB + 6], ax
    mov ax, [ru_w]
    sub ax, 8
    mov cx, ax
    add ax, [ru_x]
    mov [bx + SP_SB + 4], ax
    sub ax, RU_SBW
    mov [bx + SP_SB], ax
    sub cx, RU_SBW + CS_TEXTX + 2   ; the text's cells: up to the bar
    shr cx, 1
    shr cx, 1
    shr cx, 1
    cmp cx, CS_COLS
    jbe .cols
    mov cx, CS_COLS
.cols:
    mov [bx + SP_COLS], cx
    mov word [bx + SP_Y], CS_PANEY + 4
    mov cx, 10
    cmp byte [ru_small], 0
    je .pitch
    mov cx, 9                       ; a CGA's 200 rows: one clear scanline
.pitch:
    mov [bx + SP_PITCH], cx
    sub dx, CS_PANEY + 4 + 9        ; a line fits when its cell ends above
    mov ax, 1                       ; the frame's bottom rule
    jl .vis
    mov ax, dx
    xor dx, dx
    div cx
    inc ax
.vis:
    cmp ax, CS_LINES
    jbe .fit
    mov ax, CS_LINES
.fit:
    mov [bx + SP_VIS], ax
    call ru_spclamp
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_tabpaint - the tab's content, inside ru_paint (the ground is fresh)
cs_tabpaint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov cx, 8
    mov dx, CS_DROPY + 2
    mov si, cs_s_pick
    call ru_text
    mov cx, CS_TGTX
    mov si, cs_s_tgt
    call ru_text
    mov ax, CS_STATX                ; the state field: a frame, read-only
    mov bx, CS_DROPY
    mov cx, CS_STATX + CS_STATW * 8 + 15
    mov dx, CS_DROPY + 12
    call ru_frame
    call cs_statpaint
    mov ax, 8
    mov bx, CS_PANEY
    mov cx, [ru_w]
    sub cx, 8
    mov dx, [ru_foot]
    sub dx, 4
    call ru_frame
    mov bx, cs_pane
    call cs_panepaint
    call ru_spbar
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; cs_panepaint - BX = the pane: its visible lines, each one opaque run cut
; to the pane's width (SP_PAINT, so ru_spto scrolls it)
cs_panepaint:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov bp, bx
    mov di, [ds:bp + SP_TOP]
    mov dx, [ds:bp + SP_Y]
    mov cx, [ds:bp + SP_VIS]
.line:
    jcxz .out
    cmp di, CS_LINES
    jae .out
    push dx
    mov ax, CS_LW
    mul di
    pop dx
    add ax, cs_lines
    mov si, ax
    mov bx, [ds:bp + SP_COLS]
    mov al, [si + bx]               ; cut at the pane's width, and put back
    push ax
    mov byte [si + bx], 0
    push cx
    mov cx, CS_TEXTX
    call ru_text
    pop cx
    pop ax
    mov [si + bx], al
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

; cs_flags - the Tune foot's greying, for ru_buttons_paint: Tune is live on
; a row that can write on this machine, Save setting once something is set
cs_flags:
    push ax
    xor ax, ax
    call cs_canset
    jnc .live
    mov ax, OS88UI_DIS
.live:
    mov [ru_buttonflags], ax        ; Tune
    cmp byte [cs_setv], 0FFh
    jne .keep
    mov ax, OS88UI_DIS
.keep:
    mov [ru_buttonflags + 2], ax    ; Save setting
    mov word [ru_buttonflags + 4], 0
    mov word [ru_buttonflags + 6], 0
    mov word [ru_buttonflags + 8], OS88UI_DIS
    mov word [ru_buttonflags + 10], OS88UI_DIS
    pop ax
    ret

; cs_droppaint - the two drop-downs. Chipset is live everywhere (on an 8088
; a pick is only what the report says); Target only where Tune is. An open
; list is drawn LAST, so it lies over its neighbour
cs_droppaint:
    push bx
    push di
    xor di, di
    call cs_canset
    jnc .tgt
    mov di, OS88UI_DIS
.tgt:
    mov bx, cs_tgt
    cmp byte [cs_drop + OS88UI_DR_OPEN], 0
    je .order
    call os88ui_drop                ; the chipset list is open: target first
    xor di, di
    mov bx, cs_drop
    call os88ui_drop
    jmp short .out
.order:
    push di
    xor di, di
    mov bx, cs_drop
    call os88ui_drop
    pop di
    mov bx, cs_tgt
    call os88ui_drop
.out:
    pop di
    pop bx
    ret

; cs_drclose - take both lists down. CF = 1 either wants a repaint
cs_drclose:
    push ax
    push bx
    xor ax, ax
    mov bx, cs_drop
    call os88ui_drclose
    adc al, 0
    mov bx, cs_tgt
    call os88ui_drclose
    adc al, 0
    cmp al, 1                       ; CF = (AL < 1) - so flip it
    cmc
    pop bx
    pop ax
    ret

; cs_statpaint - the state field's text, one opaque run of CS_STATW cells
cs_statpaint:
    push cx
    push dx
    push si
    mov cx, CS_STATX + 8
    mov dx, CS_DROPY + 2
    mov si, cs_stat
    call ru_text
    pop si
    pop dx
    pop cx
    ret

; cs_redraw - the pane and the foot again, from a handler: CLIPPED (header)
cs_redraw:
    push bx
    call ru_clip
    jc .out
    call ru_geometry
    call cs_statpaint
    mov bx, cs_pane
    call cs_panepaint
    call ru_spbar
    call ru_buttons_paint
.out:
    pop bx
    ret

; cs_after - a write happened: measure what it did, compose, redraw
cs_after:
    call cs_measure
cs_recompose:
    call cs_compose
    jmp cs_redraw

; =============================================================================
; Input. The press and release halves come from ui.inc (ru_press,
; ru_mouseup) with ru_geometry run; the buttons themselves are the record's.
; =============================================================================
cs_press:                           ; CX, DX = the press. CF = 1 taken
    push ax
    push bx
    mov bx, cs_drop
    call os88ui_drpress
    call cs_dranswer
    jc .out
    mov bx, cs_tgt
    call os88ui_drpress
    call cs_dranswer
    jc .out
    mov bx, cs_pane
    call ru_sphit
.out:
    pop bx
    pop ax
    ret

cs_release:                         ; CF = 1 the drop-down took it
    push ax
    push bx
    mov bx, cs_drop
    call os88ui_drup
    call cs_dranswer
    jc .out
    mov bx, cs_tgt
    call os88ui_drup
    call cs_dranswer
.out:
    pop bx
    pop ax
    ret

; cs_dranswer - os88ui_drpress/drup's answer (AH = 1 spent, AL = the pick or
; 0FFh, CF = 1 repaint) for the record at BX, acted on. out: CF = 1 when the
; control spent it
cs_dranswer:
    jnc .drawn
    call cs_redraw                  ; the list came down with no bank behind it
.drawn:
    or ah, ah
    jz .no
    cmp al, 0FFh
    je .yes
    cmp bx, cs_tgt
    jne .chip
    mov [cs_target], al             ; the target: what the NEXT Tune sets
    jmp short .picked
.chip:
    mov [cs_pick], al               ; a pick is the user's final word (103.6.1)
.picked:
    mov byte [cs_act], 0
    call cs_recompose
.yes:
    stc
    ret
.no:
    clc
    ret

; cs_tabkey - rl_key's tail on this tab: AL = the key, AH = its scan code,
; DL = the letter upper-cased, and rl_key's eight registers pushed
cs_tabkey:
    call cs_drclose                 ; any key takes an open list down first
    jnc .key
    call cs_redraw
    jmp .out                        ; ...and Esc does nothing else
.key:
    cmp dl, 'T'
    je .write
    cmp dl, 'K'
    je .write
    cmp dl, 'S'
    je .report
    cmp dl, 'Q'
    je .quit
    call ru_clip                    ; the rest scroll the pane
    jc .out
    call ru_geometry
    mov bx, cs_pane
    mov cx, ax                      ; CL = ascii, CH = scan code
    mov ax, [bx + SP_TOP]
    cmp ch, 0x51
    je .pgdn
    cmp ch, 0x49
    je .pgup
    cmp ch, 0x50
    je .down
    cmp ch, 0x48
    je .up
    cmp ch, 0x4F
    je .end
    cmp ch, 0x47
    jne .out
    xor ax, ax
    jmp short .go
.pgdn:
    add ax, [bx + SP_VIS]
    jmp short .go
.pgup:
    sub ax, [bx + SP_VIS]
    jmp short .go
.down:
    inc ax
    jmp short .go
.up:
    dec ax
    jmp short .go
.end:
    mov ax, CS_LINES                ; ru_spto clamps to the last full page
.go:
    call ru_spto
    jmp short .out
.write:
    mov al, dl
    call cs_act_key
    jmp short .out
.report:
    call cs_report
    mov byte [cs_act], 2
    mov word [cs_amsg], cs_s_rptno
    jc .said
    mov word [cs_amsg], cs_s_rpt
.said:
    call cs_recompose
    jmp short .out
.quit:
    mov bx, [rl_win]
    call OSAPI_WM_CLOSE
.out:
    jmp ru_pop8

; =============================================================================
; Writing (SPEC.md 103.6.3, 103.6.4): Tune and Save setting. Both are
; refused on an 8088 and on a row with no apply proc - the button is greyed -
; and Tune, which writes the CHIPSET, asks once with the standard alert
; before writing to a row the probes did not find.
; =============================================================================

; cs_canset - CF = 0 the picked row can write on this machine. A row whose
; knob READS BACK (the NEAT) writes only where its probe FOUND it: its data
; port is the 8259's mask on a board that aliases 22h/23h (csne.inc), and a
; chipset that can say what it runs at has no case for the leap of faith
cs_canset:
    push ax
    push bx
    cmp byte [cs_tier], CPU_8086
    je .no
    mov al, [cs_pick]
    xor ah, ah
    mov bl, CSR_SIZE
    mul bl
    mov bx, ax
    cmp word [cs_rows + bx + CSR_APPLY], 0
    je .no
    call cs_rowdesc
    jc .no
    cmp word [bx + CSD_GET], 0
    je .yes
    mov bl, [cs_pick]
    xor bh, bh
    cmp byte [cs_pres + bx], CSO_FOUND
    jne .no
.yes:
    clc
    jmp short .out
.no:
    stc
.out:
    pop bx
    pop ax
    ret

; cs_rowdesc - BX = the picked row's sweep descriptor (csweep.inc); CF = 1
; a row that writes nothing
cs_rowdesc:
    mov bx, cs_desc_fe
    cmp byte [cs_pick], CS_F3
    je .ok
    mov bx, cs_desc_ne
    cmp byte [cs_pick], CS_NE
    je .ok
    stc
    ret
.ok:
    clc
    ret

; cs_act_key - AL = 'T' or 'K' (upper case). Two questions, each asked once
; a row: a row the probes did not find (103.6.1), and Fastest tested on a
; row nobody has run on iron (103.6.3) - the sweep is how such a row gets
; run, and what it can hang is the reason to say so first
cs_act_key:
    call cs_canset
    jc .out
    cmp al, 'K'                     ; Save setting writes a FILE, not the chipset
    je .go
    mov ah, [cs_pick]
    cmp ah, [cs_found]
    je .found
    inc ah
    cmp ah, [cs_conf]
    je .found
    push si                         ; NOT DETECTED
    mov byte [cs_pask], 0
    mov si, cs_s_conf
    jmp short .ask
.found:
    cmp al, 'T'
    jne .go
    cmp byte [cs_target], 0
    jne .go
    push ax
    push bx
    mov al, [cs_pick]
    xor ah, ah
    mov bl, CSR_SIZE
    mul bl
    mov bx, ax
    cmp byte [cs_rows + bx + CSR_STAT], CSS_VERIFIED
    pop bx
    pop ax
    je .go
    mov ah, [cs_pick]
    inc ah
    cmp ah, [cs_warn]
    je .go
    push si                         ; UNTESTED: a sweep may hang the machine
    mov byte [cs_pask], 1
    mov si, cs_s_warn
.ask:
    push bx
    push di
    mov [cs_pend], al
    mov al, OS88UI_ADANGER          ; Cancel / Proceed, Cancel the default
    mov bx, [rl_win]
    mov di, cs_conf_ans
    call os88ui_ask
    pop di
    pop bx
    pop si
    ret
.go:
    cmp al, 'T'
    jne .keep
    cmp byte [cs_target], 0         ; Tune sets what the Target names
    je cs_do_tune
    jmp cs_do_ds
.keep:
    cmp al, 'K'
    je cs_do_save
.out:
    ret

; the alert's answer: AL = 1 Proceed (OS88UI_ADANGER's second button)
cs_conf_ans:
    cmp al, 1
    jne .no
    mov al, [cs_pick]
    inc al
    cmp byte [cs_pask], 0
    jne .warn
    mov [cs_conf], al
    jmp short .again
.warn:
    mov [cs_warn], al
.again:
    mov al, [cs_pend]
    jmp cs_act_key
.no:
    mov byte [cs_act], 2
    mov word [cs_amsg], cs_s_cancl
    jmp cs_recompose

; Mfg spec: the row's book value, through its descriptor
cs_do_ds:
    push ax
    push bx
    call cs_rowdesc
    jc .out
    mov [cs_sw_desc], bx
    mov al, [bx + CSD_BOOK]
    call cs_sw_set
    mov [cs_setv], al
    mov byte [cs_meth], 1
    mov byte [cs_saved], 0
    mov byte [cs_act], 3
.out:
    pop bx
    pop ax
    jmp cs_after

; Tune: the verified sweep (csweep.inc), and what it found is SET - no
; question asked beyond the two above. The margin is the Mfg spec Target,
; and a reset is the floor
cs_do_tune:
    push ax
    push bx
    push cx
    call cs_romref                  ; CX = the slow ROM, or 0 (measures first)
    call cs_scrseg                  ; AX = the screen's segment
    xor bx, bx
    call cs_pmfind                  ; a PicoMEM's buffer, when one answers
    jc .nopm
    mov bx, [cs_pmseg]
.nopm:
    call cs_sw_refs
    call cs_rowdesc
    jc .out
    call cs_tunehome                ; AL = what the board runs now
    call cs_sw_sweep                ; AL = the fastest that read right, SET
    mov [cs_tfast], al
    mov [cs_setv], al
    mov byte [cs_meth], 3
    mov byte [cs_saved], 0
    mov byte [cs_act], 1
.out:
    pop cx
    pop bx
    pop ax
    jmp cs_after

; cs_tunehome - BX = a descriptor: AL = HOME, the setting the board runs now
; and so the one the sweep's references are read at. A chipset that reads
; back says; on a write-only one (the FE3600, the only such row) it is what
; this session set, else what the bus MEASURES (103.6.3.1), else the book
cs_tunehome:
    mov ax, [bx + CSD_GET]
    or ax, ax
    jz .meas
    jmp ax                          ; its own answer, in AL
.meas:
    mov al, [cs_setv]
    cmp al, 0FFh
    jne .ok
    mov ax, [cs_tq]
    call cs_fewaits
    jnc .ok
    mov ax, [cs_tqio]
    call cs_fewaitsio
    jnc .ok
.book:
    mov al, [bx + CSD_BOOK]
    ret
.ok:
    cmp al, [bx + CSD_FLOOR]        ; a reading under the floor is not one
    jb .book                        ; the knob can be set to
    ret

; cs_romref - CX = the SLOWEST option ROM measured, when it reads slower than
; CS_ROMSLOW over RAM; else 0. A shadowed ROM is RAM and tests nothing R4
; governs (csfe.inc's header: the Victory's VGA BIOS at C000 is one)
cs_romref:
    push ax
    push bx
    push si
    cmp byte [cs_measd], 0
    jne .have
    call cs_measure
.have:
    xor cx, cx
    mov ax, CS_ROMSLOW - 1          ; AX = the slowest so far
    mov bx, [cs_nroms]
    mov si, cs_roms
.rom:
    or bx, bx
    jz .out
    cmp [si + 4], ax
    jbe .next
    mov ax, [si + 4]
    mov cx, [si]
.next:
    add si, 6
    dec bx
    jmp short .rom
.out:
    pop si
    pop bx
    pop ax
    ret

cs_do_save:
    push ax
    push bx
    cmp byte [cs_setv], 0FFh
    je .out
    call cs_rowdesc
    jc .out
    mov al, [cs_pick]
    mov ah, [cs_meth]
    call cs_cfg_new
    mov al, [bx + CSD_KNOB]
    mov ah, [cs_setv]
    call cs_cfg_knob
    mov byte [cs_act], 2
    mov word [cs_amsg], cs_s_svno
    call cs_cfg_save
    jc .done
    mov byte [cs_saved], 1
    mov word [cs_amsg], cs_s_saved
.done:
    pop bx
    pop ax
    jmp cs_recompose
.out:
    pop bx
    pop ax
    ret

; cs_sw_measure - csweep.inc's hook: a candidate that read right is TIMED
; where it stands, interrupts still off (cs_ptime keeps them so), into
; cs_sw_ns - ns a byte over RAM for the screen, the PicoMEM and the ROM, ns
; over a NOP for the floppy status. Every register preserved
cs_sw_measure:
    push ax
    push bx
    mov ax, [cs_sw_sseg]
    xor bx, bx
    call cs_time
    mov [cs_sw_ns], ax
    mov ax, [cs_sw_pseg]
    or ax, ax
    jz .nopm
    mov bx, CS_SW_PBUF
    call cs_time
    mov [cs_sw_ns + 2], ax
.nopm:
    mov ax, [cs_sw_rseg]
    or ax, ax
    jz .norom
    xor bx, bx
    call cs_time
    mov [cs_sw_ns + 4], ax
.norom:
    call cs_iotime
    mov [cs_sw_ns + 6], ax
    pop bx
    pop ax
    ret

; cs_knobput - AL = a value of the knob the last write went through: 'R4 8'
; on the FE3600, '3 waits' on the NEAT
cs_knobput:
    push ax
    push bx
    push si
    mov bx, [cs_sw_desc]
    mov si, [bx + CSD_PRE]
    call cs_put
    xor ah, ah
    call cs_dec
    mov si, [bx + CSD_SUF]
    call cs_put
    pop si
    pop bx
    pop ax
    ret

; cs_neatws - a NEAT that its probe FOUND keeps its wait states where they
; read back: read them LIVE [NEAT p31], so the page says what the chip runs
; whoever wrote it - the BIOS, RLAPPLY or this Tune. out: CF = 0, AL = 8-bit
; wait states (2-5), AH = 16-bit (0-3); CF = 1 not a NEAT that answered
cs_neatws:
    cmp byte [cs_pick], CS_NE
    jne .no
    cmp byte [cs_pres + CS_NE], CSO_FOUND
    jne .no
    call cs_ne_get
    clc
    ret
.no:
    stc
    ret

; cs_setline - line 3: what is set, or what can be
cs_setline:
    push ax
    push si
    mov si, cs_s_xt
    cmp byte [cs_tier], CPU_8086
    je .put
    call cs_neatws                  ; a chipset that SAYS what it runs at
    jc .notread
    mov si, cs_s_rd8
    call cs_put
    push ax
    xor ah, ah
    call cs_dec
    pop ax
    mov si, cs_s_rd16
    call cs_put
    mov al, ah
    xor ah, ah
    call cs_dec
    mov si, cs_s_rdw
    call cs_put
    mov si, cs_k_none
    cmp byte [cs_setv], 0FFh        ; written this session: is it kept?
    je .put
    mov si, cs_s_rsv
    cmp byte [cs_saved], 0
    jne .put
    mov si, cs_s_rnsv
    jmp short .put
.notread:
    mov si, cs_s_nefind
    cmp byte [cs_pick], CS_NE
    je .put
    mov si, cs_s_norow
    call cs_canset
    jc .put
    mov si, cs_s_unset
    cmp byte [cs_setv], 0FFh
    je .put
    mov si, cs_s_set
    call cs_put
    mov al, [cs_setv]
    xor ah, ah
    call cs_dec
    mov al, [cs_meth]
    mov si, cs_s_m1
    cmp al, 1
    je .m
    mov si, cs_s_m3
.m:
    call cs_put
    mov si, cs_s_nsv
    cmp byte [cs_saved], 0
    je .put
    mov si, cs_s_sv
.put:
    call cs_put
    pop si
    pop ax
    ret

; cs_actline - line 4: what the last button did, or what to do first
cs_actline:
    push ax
    push bx
    push si
    mov al, [cs_act]
    cmp al, 1
    je .tune
    cmp al, 2
    je .msg
    cmp al, 3
    je .ds
    mov si, cs_s_hfe                ; nothing yet: the first thing to press
    call cs_canset
    jnc .say
    mov si, cs_s_hxt
    cmp byte [cs_tier], CPU_8086
    je .say
    cmp byte [cs_found], CS_NONE    ; found: nothing to pick
    jne .say
    mov si, cs_s_hpick
.say:
    call cs_put
    jmp short .out
.msg:
    mov si, [cs_amsg]
    call cs_put
    cmp si, cs_s_rpt                ; a report: its name after the sentence
    jne .out
    mov si, [cs_rname]
    call cs_put
    jmp short .out
.ds:
    mov si, cs_s_ds
    call cs_put
    mov bx, [cs_sw_desc]
    mov al, [bx + CSD_BOOK]
    call cs_knobput
    mov si, cs_s_ds2
    jmp short .last
.tune:
    mov si, cs_s_tune
    call cs_put
    mov al, [cs_tfast]
    call cs_knobput
    mov si, cs_s_tune2
    call cs_put
    mov si, cs_s_tfl
    call cs_put
    mov bx, [cs_sw_desc]
    mov al, [bx + CSD_FLOOR]
    mov si, cs_s_tnone
    cmp byte [cs_sw_fail], 0
    je .floor
    mov al, [cs_sw_fail]
    call cs_knobput
    mov si, cs_s_tfl2
    call cs_put
    mov ax, [cs_sw_bad]
    call cs_dec
    mov si, cs_s_tfl3
    jmp short .last
.floor:
    call cs_put
    call cs_knobput
    mov si, cs_k_none
.last:
    call cs_put
.out:
    pop si
    pop bx
    pop ax
    ret

; cs_clockline - line 6: the CPU clock, the chipset's read in its clocks
; and, on a row whose book gives the sum, the setting that read means
cs_clockline:
    push ax
    push si
    mov si, cs_s_ck88
    cmp byte [cs_tier], CPU_8086
    je .put
    mov si, cs_s_cknone
    mov ax, [cs_mhz]
    or ax, ax
    jz .put
    mov si, cs_s_ck
    call cs_put
    call cs_dec100                  ; MHz, two places
    mov si, cs_s_ck2
    call cs_put
    mov si, cs_s_ckno8
    mov ax, [cs_tq]
    cmp ax, CS_NOMEAS
    je .put
    mov si, cs_s_ck3
    call cs_put
    call cs_dec10                   ; clocks, one place
    mov si, cs_s_ck4
    call cs_put
    cmp byte [cs_pick], CS_F3       ; the sum is the FE3600's book
    jne .out
    mov ax, [cs_tq]
    call cs_fewaits
    jc .out
    mov si, cs_s_ck5
    call cs_put
    xor ah, ah
    call cs_dec
    jmp short .out
.put:
    call cs_put
.out:
    pop si
    pop ax
    ret

; cs_ioline - line 7: the 8-bit I/O read, in ns and in CPU clocks
cs_ioline:
    push ax
    push cx
    push dx
    push si
    mov ax, [cs_nsio]
    cmp ax, CS_NOMEAS
    je .out                         ; an 8088, or nothing timed: a blank line
    mov si, cs_s_io
    call cs_put
    call cs_dec
    mov si, cs_s_io2
    call cs_put
    mov ax, [cs_tqio]
    cmp ax, CS_NOMEAS
    je .out
    mov si, cs_s_io3
    call cs_put
    call cs_dec10
    mov si, cs_s_ck4
    call cs_put
    cmp byte [cs_pick], CS_F3       ; the FE3600: its R4, read a second way
    jne .out
    mov ax, [cs_tqio]
    call cs_fewaitsio
    jc .out
    mov si, cs_s_ck5
    call cs_put
    xor ah, ah
    call cs_dec
.out:
    pop si
    pop dx
    pop cx
    pop ax
    ret

cs_dec100:                          ; AX = x100: "16.00"
    push ax
    push bx
    push dx
    mov bx, 100
    jmp short cs_decf
cs_dec10:                           ; AX = x10: "14.6"
    push ax
    push bx
    push dx
    mov bx, 10
cs_decf:
    xor dx, dx
    div bx
    call cs_dec
    mov al, '.'
    call cs_ch1
    mov ax, dx
    cmp bx, 100
    jne .one
    cmp al, 10
    jae .one
    push ax
    mov al, '0'
    call cs_ch1
    pop ax
.one:
    call cs_dec
    pop dx
    pop bx
    pop ax
    ret

; cs_scrseg - AX = the screen's segment
cs_scrseg:
    push bx
    push cx
    push dx
    call OSAPI_VIDEO                ; DL = the adapter
    mov ax, 0xA000
    cmp dl, VID_HERC
    jne .notherc
    mov ax, 0xB000
.notherc:
    cmp dl, VID_CGA
    jne .out
    mov ax, 0xB800
.out:
    pop dx
    pop cx
    pop bx
    ret

; =============================================================================
; Measuring: an 8-bit read in ns a byte, net of the same read from RAM
; (bl_time, PIT counts of 838.1 ns, 8 passes of 512 bytes)
; =============================================================================
cs_rd512:
    push cx
    push si
    push ds
    mov si, [cs_toff]
    mov ds, [cs_tseg]
    mov cx, 512
    cld
    rep lodsb
    pop ds
    pop si
    pop cx
    ret

; cs_ptime - CX passes of [bl_body], each between two PIT reads: DX:AX =
; the counts. bl_time's method P without its tick reads, which are far calls
; into the kernel: this one runs inside the sweep's cli window too, and the
; interrupt flag is left as it was found
cs_ptime:
    push bx
    push cx
    push si
    push di
    xor si, si
    xor di, di
.p:
    push cx
    push si
    push di
    pushf
    cli
    call bl_pit
    mov [bl_prev], ax
    call word [bl_body]
    call bl_pit
    popf
    pop di
    pop si
    mov bx, [bl_prev]
    sub bx, ax                      ; a DOWN-counter: start - end, modular
    add si, bx
    adc di, 0
    pop cx
    loop .p
    mov ax, si
    mov dx, di
    pop di
    pop si
    pop cx
    pop bx
    ret

; cs_time - AX = segment, BX = offset: ns a byte over RAM, in AX
cs_time:
    push bx
    push cx
    push dx
    mov [cs_tseg], ax
    mov [cs_toff], bx
    mov word [bl_body], cs_rd512
    mov cx, 8
    call cs_ptime                   ; DX:AX = the counts
    sub ax, [cs_ram]
    jnc .pos
    xor ax, ax
.pos:
    mov cx, 8381                    ; counts x 838.1 ns / (8 x 512 bytes)
    mul cx
    mov cx, 40960
    div cx
    pop dx
    pop cx
    pop bx
    ret

cs_measure:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov ax, ds                      ; the RAM twin: our own line buffer
    mov [cs_tseg], ax
    mov word [cs_toff], cs_lines
    mov word [bl_body], cs_rd512
    mov cx, 8
    call cs_ptime
    mov [cs_ram], ax
    call cs_scrseg
    xor bx, bx
    call cs_time
    mov [cs_nsscr], ax
    mov word [cs_nspm], CS_NOMEAS     ; a PicoMEM: AT-class only (103.6.1),
    cmp byte [cs_tier], CPU_8086    ; its counting port first, then its BIOS
    je .roms
    call cs_pmfind
    jc .roms
    mov ax, [cs_pmseg]
    mov bx, 0x5000                  ; its disk transfer buffer
    call cs_time
    mov [cs_nspm], ax
.roms:                              ; option ROMs, C000-EFFF in 2 KB steps
    mov word [cs_nroms], 0
    mov si, cs_roms
    mov ax, 0xC000
.scan:
    cmp ax, 0xF000
    jae .done
    mov es, ax
    cmp word [es:0], 0xAA55
    jne .step
    mov cx, [cs_nroms]
    cmp cx, CS_NROMS
    jae .done
    mov [si], ax
    xor bx, bx
    mov bl, [es:2]                  ; its length in 512-byte blocks
    mov cx, bx
    shr bx, 1
    mov [si + 2], bx                ; ...as KB
    push ax
    xor bx, bx
    call cs_time
    mov [si + 4], ax
    pop ax
    add si, 6
    inc word [cs_nroms]
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1
    shl cx, 1                       ; blocks x 32 paragraphs
    add cx, 0x7F
    and cx, 0xFF80                  ; rounded up to the 2 KB step
    jz .step
    add ax, cx
    jmp short .scan
.step:
    add ax, 0x80
    jmp short .scan
.done:
    call cs_clock                   ; the CPU clock, and the read in its clocks
    mov byte [cs_measd], 1
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; THE READ IN CPU CLOCKS (SPEC.md 103.6.3.1). An 8-bit cycle's length is a
; count of the CHIPSET's clocks, so ns alone says nothing about a setting
; until the clock is known. The clock is timed here rather than taken from a
; benchmark Run: REDLINE's own MUL body (64 x `mov ax, imm / mul bx`, 23
; clocks a pair on a 286 and 24 on a 386 by the book, rl_mulbook) against
; the PIT - about 6 ms at 16 MHz. An EMPTY pass is timed first and taken
; off: every pass's span also holds the PIT reads around it, two or three
; AT-bus I/O cycles of a microsecond each, and with them in the count the
; PB286LP's 12 MHz 286 timed at 10.77.
;
; [cs_mhz] = MHz x 100, or 0 on an 8088 (nothing to tune, nothing to time).
; [cs_rd8] = the FASTEST source that reads slower than CS_ROMSLOW - an 8-bit
;   device: a 16-bit card reads near RAM speed, and a slow device can only
;   ADD waits of its own (IOCHRDY), never take the chipset's away, so the
;   fastest 8-bit source is the one that is the chipset's cycle and nothing
;   else. CS_NOMEAS when no source qualifies.
; [cs_tq] = [cs_rd8] in tenths of a CPU clock, or CS_NOMEAS.
; =============================================================================
cs_clock:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov word [cs_mhz], 0
    mov word [cs_tq], CS_NOMEAS
    mov word [cs_rd8], CS_NOMEAS
    mov word [cs_nsio], CS_NOMEAS
    mov word [cs_tqio], CS_NOMEAS
    mov bx, 23
    cmp byte [cs_tier], CPU_286
    je .time
    mov bx, 24
    cmp byte [cs_tier], CPU_386
    jne .out                        ; an 8088: no clock wanted
.time:
    push bx
    mov word [bl_n], 64
    mov word [bl_body], cs_noop
    call bl_time                    ; DX:AX = the passes' own cost
    mov si, ax
    mov di, dx
    mov [cs_rawe], ax
    mov word [bl_body], rl_mul
    call bl_time                    ; DX:AX = PIT counts, 64 passes
    mov [cs_rawm], ax
    sub ax, si
    sbb dx, di
    jnc .net
    xor ax, ax                      ; a noisy empty pass: no clock, not junk
    xor dx, dx
.net:
    mov cx, 64
    call bl_div32                   ; counts a pass (clobbers BX)
    mov cx, ax
    pop ax
    jcxz .out
    push cx
    mov cx, 7636                    ; 64 x 1.193182 x 100: rl_estimate's
    mul cx                          ; arithmetic, MHz x 100
    pop cx
    cmp dx, cx
    jae .out                        ; would overflow: not a number to trust
    div cx
    mov [cs_mhz], ax
    call cs_iotime                  ; the 8-bit I/O read, beside the memory
    mov [cs_nsio], ax
    call cs_ckq
    mov [cs_tqio], ax
    mov ax, [cs_ion]
    mov [cs_rawn], ax
    mov ax, [cs_ioi]
    mov [cs_rawi], ax
    mov ax, CS_NOMEAS               ; the fastest 8-bit source
    mov cx, [cs_nsscr]
    call .take
    mov cx, [cs_nspm]
    call .take
    mov bx, [cs_nroms]
    mov si, cs_roms
.rom:
    or bx, bx
    jz .have
    mov cx, [si + 4]
    call .take
    add si, 6
    dec bx
    jmp short .rom
.have:
    mov [cs_rd8], ax
    cmp ax, CS_NOMEAS
    je .out
    mul word [cs_mhz]               ; ns x MHz x 100 / 10000 = tenths of a clock
    mov cx, 10000
    cmp dx, cx
    jae .out
    div cx
    mov [cs_tq], ax
.out:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.take:                              ; CX = a source's ns: AX = the least >= floor
    cmp cx, CS_NOMEAS
    je .no
    cmp cx, CS_ROMSLOW
    jb .no
    cmp cx, ax
    jae .no
    mov ax, cx
.no:
    ret
cs_noop:                            ; the empty pass's body
    ret

; cs_ckq - AX = ns (or CS_NOMEAS): AX = tenths of a CPU clock at [cs_mhz],
; or CS_NOMEAS. Preserves every other register
cs_ckq:
    cmp ax, CS_NOMEAS
    je .out
    push cx
    push dx
    mul word [cs_mhz]               ; ns x MHz x 100 / 10000
    mov cx, 10000
    cmp dx, cx
    jae .no
    div cx
    jmp short .done
.no:
    mov ax, CS_NOMEAS
.done:
    pop dx
    pop cx
.out:
    ret

; -----------------------------------------------------------------------------
; cs_iotime - AX = one 8-bit I/O READ, in ns over a NOP (SPEC.md
; 103.6.3.1). A memory source can be a device that holds the bus with
; IOCHRDY - the PB286LP's VGA reads at 2,484 ns, ~13 bus clocks against the
; chipset's ~6 - and with shadow RAM fixed on, a board may have no other.
; The floppy controller's MAIN STATUS register is on every AT, 8 bits wide,
; and a read of it has no side effect [765 data sheet], so it is the
; chipset's own 8-bit I/O cycle and nothing else. 64 `in al, dx` against 64
; `nop`s, both one byte, so the fetch is the same: the difference is the
; I/O bus cycle less the two clocks a NOP is shorter than an IN. AT only -
; on an 8088 REDLINE touches no port at all (103.6.1).
; -----------------------------------------------------------------------------
cs_iotime:                          ; out AX = ns, or CS_NOMEAS
    push cx
    push dx
    push si
    mov word [bl_body], cs_ionop
    mov cx, 64
    call cs_ptime
    mov [cs_ion], ax
    mov si, ax
    mov word [bl_body], cs_ioin
    mov cx, 64
    call cs_ptime
    mov [cs_ioi], ax
    sub ax, si
    jc .no
    mov cx, 8381                    ; counts x 838.1 ns / (64 x 64 reads)
    mul cx
    mov cx, 40960
    cmp dx, cx
    jae .no
    div cx
    jmp short .out
.no:
    mov ax, CS_NOMEAS
.out:
    pop si
    pop dx
    pop cx
    ret
cs_ioin:
    mov dx, CS_IOPORT
%rep 64
    in al, dx
%endrep
    ret
cs_ionop:
    mov dx, CS_IOPORT
%rep 64
    nop
%endrep
    ret

; cs_pmfind - a PicoMEM? Its test port 2A3h counts up by one a read, which an
; empty bus does not; only then is its BIOS asked where it lives (int 13h
; AX=6000h DX=1234h answers BX = its segment, DX = AA55h). CF = 1 none.
cs_pmfind:
    push ax
    push bx
    push cx
    push dx
    mov dx, 0x2A3
    pushf
    cli
    in al, dx
    mov ah, al
    mov cx, 8
.ramp:
    in al, dx
    inc ah
    cmp al, ah
    jne .none
    loop .ramp
    popf
    mov ax, 0x6000
    mov dx, 0x1234
    int 0x13
    cmp dx, 0xAA55
    jne .no
    mov [cs_pmseg], bx
    clc
    jmp short .out
.none:
    popf
.no:
    stc
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Composing the report: CS_LINES slots, each padded to CS_COLS so a line is
; drawn opaque across its width and needs no erase pass
; =============================================================================
cs_lbeg:                            ; AX = the line: start it empty
    push ax
    push cx
    push di
    mov cx, CS_LW
    mul cx
    add ax, cs_lines
    mov [cs_lcur], ax
    add ax, CS_COLS
    mov [cs_lend], ax
    pop di
    pop cx
    pop ax
    ret

cs_put:                             ; SI = NUL text, appended
    push ax
    push si
    push di
    mov di, [cs_lcur]
.c:
    lodsb
    or al, al
    jz .e
    cmp di, [cs_lend]
    jae .e
    mov [di], al
    inc di
    jmp short .c
.e:
    mov [cs_lcur], di
    pop di
    pop si
    pop ax
    ret

cs_ch1:                             ; AL = one character
    push di
    mov di, [cs_lcur]
    cmp di, [cs_lend]
    jae .e
    mov [di], al
    inc word [cs_lcur]
.e:
    pop di
    ret

cs_hex2:                            ; AL as two hex digits
    push ax
    push cx
    mov ah, al
    mov cl, 4
    shr al, cl
    call .d
    mov al, ah
    and al, 0x0F
    call .d
    pop cx
    pop ax
    ret
.d:
    add al, '0'
    cmp al, '9'
    jbe .w
    add al, 7
.w:
    jmp cs_ch1

cs_hex4:                            ; AX as four
    push ax
    xchg al, ah
    call cs_hex2
    xchg al, ah
    call cs_hex2
    pop ax
    ret

cs_dec:                             ; AX unsigned
    ; STKBALANCE-LOOP: one digit pushed a turn and the second loop pops them; the count is in CX
    push ax
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
.div:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .div
.out:
    pop ax
    add al, '0'
    call cs_ch1
    loop .out
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cs_lfin:                            ; pad with spaces and close the line
    push ax
    push di
    mov di, [cs_lcur]
    mov al, ' '
.p:
    cmp di, [cs_lend]
    jae .z
    mov [di], al
    inc di
    jmp short .p
.z:
    mov byte [di], 0
    pop di
    pop ax
    ret

cs_rowname:                         ; AL = a row: SI = its name
    push ax
    push bx
    xor ah, ah
    mov bl, CSR_SIZE
    mul bl
    mov bx, ax
    mov si, [cs_rows + bx + CSR_NAME]
    pop bx
    pop ax
    ret

; cs_statcomp - the state field: what the bus runs at NOW, padded to its
; cells, named against the row's book: under it Tuned, at it Mfg spec, over
; it Stock. A chipset that reads back (the NEAT) is read live; on the
; write-only FE3600 it is what this session wrote, or else a MEASUREMENT
; marked '~' - REDLINE cannot read the registers to know who set them
cs_statcomp:
    push ax
    push bx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, cs_stat
    mov si, cs_st_stock
    call cs_rowdesc                 ; BX = the row's book
    jc .last
    call cs_neatws                  ; a chipset that says what it runs at
    jnc .live
    cmp byte [cs_setv], 0FFh
    jne .set
    cmp byte [cs_pick], CS_F3       ; nothing set: what the BUS says, on the
    jne .last                       ; row whose book gives the sum
    mov ax, [cs_tq]                 ; the memory read first; a board with no
    call cs_fewaits                 ; clean 8-bit memory source reads the
    jnc .meas                       ; I/O one
    mov ax, [cs_tqio]
    call cs_fewaitsio
    jc .last
.meas:
    mov ah, '~'                     ; measured, not written by us
    jmp short .name
.set:
    mov al, [cs_setv]
.live:
    xor ah, ah
.name:
    mov si, cs_st_tuned
    cmp al, [bx + CSD_BOOK]
    jb .head
    mov si, cs_st_spec
    je .head
    mov si, cs_st_stk
.head:
    push ax
    call .copy                      ; (lodsb: AL)
    pop ax
    or ah, ah
    jz .num
    xchg al, ah
    stosb
    xchg al, ah
.num:
    cmp al, 10
    jb .one
    mov byte [di], '1'
    inc di
    sub al, 10
.one:
    add al, '0'
    stosb
    mov si, cs_st_waits
.last:
    call .copy
    mov al, ' '
.pad:
    cmp di, cs_stat + CS_STATW
    jae .end
    stosb
    jmp short .pad
.end:
    mov byte [di], 0
    pop es
    pop di
    pop si
    pop bx
    pop ax
    ret
.copy:
    lodsb
    or al, al
    jz .copied
    stosb
    jmp short .copy
.copied:
    ret

cs_compose:
    push ax
    push bx
    push cx
    push si
    call cs_statcomp
    xor ax, ax                      ; 0: the machine
    call cs_lbeg
    mov si, cs_s_cpu
    call cs_put
    mov al, [cs_tier]
    mov si, cs_s_88
    cmp al, CPU_8086
    je .t
    mov si, cs_s_286
    cmp al, CPU_286
    je .t
    mov si, cs_s_386
.t:
    call cs_put
    mov si, cs_s_bios
    call cs_put
    mov cx, 8
    mov si, cs_date
.dt:
    lodsb
    cmp al, ' '
    jb .dtq
    cmp al, '~'
    jbe .dtc
.dtq:
    mov al, '?'
.dtc:
    call cs_ch1
    loop .dt
    mov si, cs_s_model
    call cs_put
    mov al, [cs_model]
    call cs_hex2
    mov si, cs_s_fp
    call cs_put
    mov ax, [cs_crc]
    call cs_hex4
    call cs_lfin
    mov ax, 1                       ; 1: what was found
    call cs_lbeg
    mov si, cs_s_det
    call cs_put
    mov al, [cs_found]
    cmp al, CS_NONE
    jne .found
    mov si, cs_s_nodet
    call cs_put
    jmp short .l2
.found:
    call cs_rowname
    call cs_put
.l2:
    call cs_lfin
    mov ax, 2                       ; 2: what is picked, and its status
    call cs_lbeg
    mov si, cs_s_pick2
    call cs_put
    mov al, [cs_pick]
    call cs_rowname
    call cs_put
    mov si, cs_s_stat
    call cs_put
    mov al, [cs_pick]
    xor ah, ah
    mov bl, CSR_SIZE
    mul bl
    mov bx, ax
    mov al, [cs_rows + bx + CSR_STAT]
    mov si, cs_s_sid
    cmp al, CSS_IDENTIFY
    je .st
    mov si, cs_s_sds
    cmp al, CSS_DATASHEET
    je .st
    mov si, cs_s_sver
.st:
    call cs_put
    call cs_lfin
    mov ax, 3                       ; 3: the setting
    call cs_lbeg
    call cs_setline
    call cs_lfin
    mov ax, 4                       ; 4: what the last button did
    call cs_lbeg
    call cs_actline
    call cs_lfin
    mov ax, 5                       ; 5: the 8-bit read
    call cs_lbeg
    mov si, cs_s_read
    call cs_put
    cmp byte [cs_measd], 0
    jne .meas
    mov si, cs_s_nomeas
    call cs_put
    jmp short .l5
.meas:
    mov si, cs_s_scr
    call cs_put
    mov ax, [cs_nsscr]
    call cs_dec
    cmp word [cs_nspm], CS_NOMEAS
    je .l5
    mov si, cs_s_pm
    call cs_put
    mov ax, [cs_nspm]
    call cs_dec
.l5:
    call cs_lfin
    mov ax, 6                       ; 6: the clock, and the read in it
    call cs_lbeg
    call cs_clockline
    call cs_lfin
    mov ax, 7                       ; 7: the 8-bit I/O read
    call cs_lbeg
    call cs_ioline
    call cs_lfin
    mov ax, 8                       ; 8: the option ROMs
    call cs_lbeg
    mov si, cs_s_roms
    call cs_put
    xor bx, bx
    mov si, cs_roms
.rom:
    cmp bx, [cs_nroms]
    jae .roms
    cmp bx, 3                       ; three fit the line (the report too)
    jae .roms
    mov ax, [si]
    call cs_hex4
    mov al, ' '
    call cs_ch1
    mov ax, [si + 2]
    call cs_dec
    push si
    mov si, cs_s_kb
    call cs_put
    pop si
    mov ax, [si + 4]
    call cs_dec
    push si
    mov si, cs_s_ns
    call cs_put
    pop si
    add si, 6
    inc bx
    jmp short .rom
.roms:
    call cs_lfin
    mov ax, CS_HELP0 - 1            ; a blank, then the help
    mov si, cs_help
.help:
    call cs_lbeg
    push si
    mov si, [si]
    call cs_put
    pop si
    call cs_lfin
    add si, 2
    inc ax
    cmp ax, CS_HELP0 + CS_HELPN
    jb .help
    mov ax, CS_HELP0 + CS_HELPN     ; then the probes, one line each
    call cs_lbeg
    mov si, cs_s_probes
    call cs_put
    mov al, [cs_aliased]
    or al, al
    jz .noal
    mov si, cs_s_alias
    call cs_put
.noal:
    call cs_lfin
    mov bx, 1
.p:
    cmp bx, CS_NROWS
    jae .done
    mov ax, bx
    add ax, CS_HELP0 + CS_HELPN
    call cs_lbeg
    mov si, cs_s_ind
    call cs_put
    mov al, bl
    call cs_rowname
    call cs_put
    mov al, ':'
    call cs_ch1
    mov al, ' '
    call cs_ch1
    mov al, [cs_pres + bx]
    xor ah, ah
    mov si, ax
    shl si, 1
    mov si, [cs_s_out + si]
    call cs_put
    cmp byte [cs_pres + bx], CSO_ABSENT
    jb .pfin
    cmp byte [cs_pres + bx], CSO_FOUND
    ja .pfin
    mov si, cs_s_read2
    call cs_put
    mov si, bx
    shl si, 1
    mov ax, [cs_pval + si]
    call cs_hex4
.pfin:
    call cs_lfin
    inc bx
    jmp short .p
.done:
    call cs_fieldlines
    pop si
    pop cx
    pop bx
    pop ax
    ret
%if CS_XTRA0 + CS_XTRAN != CS_LINES
    %error CS_LINES is not the lines cs_compose writes
%endif

; -----------------------------------------------------------------------------
; cs_fieldlines - the lines that are for whoever reads the REPORT (103.6.3):
; one field run is meant to carry everything the next design needs, so the
; BIOS's own words, every register a chipset lets us read, the raw PIT counts
; the clock and the I/O read were worked out from, and every step the last
; sweep took, with what failed and the times of what passed
; -----------------------------------------------------------------------------
cs_fieldlines:
    push ax
    push bx
    push cx
    push si
    call cs_btscan
    mov ax, CS_XTRA0                ; the BIOS's text, two strings
    mov bx, cs_bt
.bt:
    call cs_lbeg
    mov si, cs_s_btxt
    call cs_put
    mov si, bx
    cmp byte [si], 0
    jne .btp
    mov si, cs_s_btnone
.btp:
    call cs_put
    call cs_lfin
    inc ax
    add bx, CS_BTW
    cmp bx, cs_bt + 2 * CS_BTW
    jb .bt
    call cs_lbeg                    ; the registers
    mov si, cs_s_noregs
    cmp byte [cs_pres + CS_NE], CSO_FOUND
    jne .rgput
    mov si, cs_s_neregs
    call cs_put
    push ax
    mov ah, 0x60
.rg:
    mov al, ' '
    call cs_ch1
    pushf
    cli
    mov al, ah
    out 0x22, al
    in al, 0x23
    popf
    call cs_hex2
    inc ah
    cmp ah, 0x70
    jb .rg
    pop ax
    mov si, cs_k_none
.rgput:
    call cs_put
    call cs_lfin
    inc ax
    call cs_lbeg                    ; the raw counts
    push ax
    mov bx, cs_rawtab
.raw:
    mov si, [bx]
    call cs_put
    mov si, [bx + 2]
    mov ax, [si]
    call cs_dec
    add bx, 4
    cmp bx, cs_rawtab + 4 * 5
    jb .raw
    pop ax
    call cs_lfin
    inc ax
    call cs_lbeg                    ; the sweep: where it started
    mov si, cs_s_swno
    cmp byte [cs_sw_n], 0
    je .swput
    mov si, cs_s_swh
    call cs_put
    push ax
    mov al, [cs_sw_home]
    call cs_knobput
    mov si, cs_s_swf
    call cs_put
    mov al, [cs_sw_fdc]
    mov si, cs_s_swnone
    cmp al, 0FFh
    je .fdc
    call cs_hex2
    mov si, cs_k_none
.fdc:
    call cs_put
    mov si, cs_s_swp
    call cs_put
    mov si, cs_s_swyes
    cmp byte [cs_sw_pg], 0
    jne .pg
    mov si, cs_s_swno2
.pg:
    call cs_put
    mov bx, cs_swsegs
.seg:
    mov si, [bx]
    call cs_put
    mov si, [bx + 2]
    mov ax, [si]
    call cs_hex4
    add bx, 4
    cmp bx, cs_swsegs + 4 * 3
    jb .seg
    pop ax
    mov si, cs_k_none
.swput:
    call cs_put
    call cs_lfin
    xor bx, bx                      ; ...and every step
    mov si, cs_sw_log
.step:
    inc ax
    call cs_lbeg
    cmp bl, [cs_sw_n]
    jae .sfin
    call cs_swrow
.sfin:
    call cs_lfin
    add si, CSL_SIZE
    inc bx
    cmp bx, CS_SWMAX
    jb .step
    pop si
    pop cx
    pop bx
    pop ax
    ret

; cs_swrow - SI = a step of cs_sw_log: its line
cs_swrow:
    push ax
    push bx
    push cx
    push si
    mov al, ' '
    call cs_ch1
    call cs_ch1
    mov al, [si + CSL_VAL]
    call cs_knobput
    mov bx, si
    cmp word [bx + CSL_BAD], 0
    jne .fail
    mov si, cs_s_swok
    call cs_put
    xor cx, cx
.ns:
    mov si, cx
    shl si, 1
    mov si, [cs_swnames + si]
    call cs_put
    mov si, cx
    shl si, 1
    mov ax, [bx + CSL_NS + si]
    cmp ax, CS_NOMEAS
    jne .num
    mov al, '-'
    call cs_ch1
    jmp short .nx
.num:
    call cs_dec
.nx:
    inc cx
    cmp cx, 4
    jb .ns
    jmp short .out
.fail:
    mov si, cs_s_swfail
    call cs_put
    mov ax, [bx + CSL_BAD]
    call cs_dec
    mov si, cs_s_swwrong
    call cs_put
    mov al, [bx + CSL_WHY]
    mov cx, 7
    mov si, cs_swwhy
.why:
    shr al, 1
    jnc .wn
    push si
    mov si, [si]
    call cs_put
    pop si
.wn:
    add si, 2
    loop .why
.out:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; cs_btscan - once: the first two printable runs in the BIOS segment that
; hold "COPYR" or "COPR" (any case), from the start of the run, into cs_bt.
; It is how the Compaq Portable III was told apart (REDLINE-TUNE-PLAN), so
; the report carries it rather than the next owner having to ask for a dump
cs_btscan:
    cmp byte [cs_btdone], 0
    jne .ret
    mov byte [cs_btdone], 1
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    push ds
    pop es
    cld
    mov di, cs_bt
    xor al, al                      ; both slots empty until a string lands
    mov [di], al
    mov [di + CS_BTW], al
    mov bx, 2                       ; strings to take
    xor si, si
    cmp byte [cs_tier], CPU_8086    ; an 8088 scans the top 8 KB, where a
    jne .seg                        ; PC's ROM keeps it: the whole 64 KB is
    mov si, 0xE000                  ; most of a second at 4.77 MHz
.seg:
    mov ax, 0xF000
    mov ds, ax                      ; DS = the BIOS; ES:DI ours
.scan:
    cmp si, 0xFFF0
    jae .done
    mov al, [si]
    or al, 0x20
    cmp al, 'c'
    jne .nx
    mov ax, [si + 1]
    or ax, 0x2020
    cmp ax, 'op'
    jne .nx
    mov ax, [si + 3]
    or ax, 0x2020
    cmp ax, 'yr'                    ; COPYRIGHT, not SeaBIOS's "Copying"
    je .hit
    cmp ax, 'r.'                    ; COPR. with its dot, not the Compaq's
    jne .nx                         ; "80287 Coprocessor Error"
.hit:
    mov cx, 16                      ; back toward the start of its run
.back:
    or si, si
    jz .copy
    mov al, [si - 1]
    cmp al, ' '
    jb .copy
    cmp al, '~'
    ja .copy
    dec si
    loop .back
.copy:
    mov cx, CS_BTW - 1
.cp:
    lodsb
    cmp al, ' '
    jb .end
    cmp al, '~'
    ja .end
    stosb
    loop .cp
.end:
    xor al, al
    stosb
.skip:                              ; past the rest of this run
    cmp si, 0xFFF0
    jae .done
    mov al, [si]
    cmp al, ' '
    jb .next
    cmp al, '~'
    ja .next
    inc si
    jmp short .skip
.next:
    mov di, cs_bt + CS_BTW
    dec bx
    jnz .scan
    jmp short .done
.nx:
    inc si
    jmp short .scan
.done:
    pop ds
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
.ret:
    ret

cs_rawtab:  dw cs_s_raw, cs_ram, cs_s_rawe, cs_rawe, cs_s_rawm, cs_rawm
            dw cs_s_rawn, cs_rawn, cs_s_rawi, cs_rawi
cs_swsegs:  dw cs_s_swscr, cs_sw_sseg, cs_s_swrom, cs_sw_rseg
            dw cs_s_swpm, cs_sw_pseg
cs_swnames: dw cs_s_nscr, cs_s_npm, cs_s_nrom, cs_s_nio
cs_swwhy:   dw cs_s_wscr, cs_s_wfdc, cs_s_wpage, cs_s_wrb  ; CSW_ bit order
            dw cs_s_wrom, cs_s_wpm, cs_s_wramp

; =============================================================================
; cs_report - every line but the help, CR LF, into bl_out, written as
; CHIPSET.TXT numbered (CHIPSET1.TXT, CHIPSET2.TXT... - benchlib's
; bl_numname). out: CF = 1 the write failed; [cs_rname] = the name
; =============================================================================
cs_report:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, bl_out
    xor bx, bx
.line:
    cmp bx, CS_LINES
    jae .write
    cmp bx, CS_HELP0                ; the help is the page's, not the report's
    jne .take
    mov bx, CS_HELP0 + CS_HELPN
.take:
    mov ax, bx
    mov cx, CS_LW
    mul cx
    add ax, cs_lines
    mov si, ax
    mov dx, di                      ; where this line starts
.c:
    lodsb
    or al, al
    jz .eol
    stosb
    jmp short .c
.eol:
    cmp di, dx                      ; trim the padding
    je .crlf
    cmp byte [di - 1], ' '
    jne .crlf
    dec di
    jmp short .eol
.crlf:
    mov ax, 0x0A0D
    stosw
    inc bx
    jmp short .line
.write:
    mov cx, di
    sub cx, bl_out
    xor dx, dx
    mov bx, bl_out
    mov si, cs_s_file
    call bl_numname                 ; SI = the first CHIPSETn.TXT not there
    mov [cs_rname], si
    call OSAPI_FILE_WRITE
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

cs_help:    dw cs_s_blank, cs_h0, cs_h1, cs_h2, cs_h3, cs_h4, cs_h5, cs_h6
            dw cs_h7
%if ($ - cs_help) != 2 * (CS_HELPN + 1)
    %error cs_help is not CS_HELPN lines and the blank before them
%endif
cs_s_blank: db 0
cs_h0: db 'Tune sets the 8-bit bus to the Target. Mfg spec is the data sheet', 27h, 's', 0
cs_h1: db 'value, safe on any board. Fastest tested steps down a wait state at a', 0
cs_h2: db 'time, reading back the screen, a slow ROM, a PicoMEM, the floppy status', 0
cs_h3: db 'and an I/O write, and keeps the fastest that read right. It cannot test', 0
cs_h4: db 'video writes, the keyboard, clock, serial ports or an 8-bit disk card:', 0
cs_h5: db 'keep backups, and if anything misbehaves, Tune to Mfg spec. A hang', 0
cs_h6: db 'needs a reset, which puts the BIOS timing back. Save setting keeps it', 0
cs_h7: db 'for SYSTEM\RLAPPLY.O88 to put back after each boot.', 0

cs_s_file:  db 'CHIPSET.TXT', 0
cs_s_cpu:   db 'CPU ', 0
cs_s_88:    db '8088/8086', 0
cs_s_286:   db '286-class', 0
cs_s_386:   db '386-class', 0
cs_s_bios:  db '  BIOS ', 0
cs_s_model: db ' model ', 0
cs_s_fp:    db '  fingerprint ', 0
cs_s_det:   db 'Detected: ', 0
cs_s_nodet: db 'nothing - pick it above if you know it', 0
cs_s_pick2: db 'Picked:   ', 0
cs_s_stat:  db '  status ', 0
cs_s_sid:   db 'IDENTIFY (reports, writes nothing)', 0
cs_s_sds:   db 'DATASHEET (book values, never run)', 0
cs_s_sver:  db 'VERIFIED (run on real hardware)', 0
cs_s_xt:    db 'An 8088/8086: the bus is the CPU', 27h, 's own, nothing to tune.', 0
cs_s_rd8:   db 'Set now (read back): 8-bit ', 0
cs_s_rd16:  db ' waits, 16-bit ', 0
cs_s_rdw:   db ' waits', 0
cs_s_norow: db 'This chipset is identified only: nothing on it can be set.', 0
cs_s_unset: db 'Set now: nothing - the BIOS', 27h, 's own timing is running', 0
cs_s_set:   db 'Set now: R4 ', 0
cs_s_m1:    db ' (maker', 27h, 's spec)', 0
cs_s_m3:    db ' (fastest tested)', 0
cs_s_sv:    db ', saved for RLAPPLY', 0
cs_s_nsv:   db ' - not saved: Save setting keeps it', 0
cs_s_hfe:   db 'Choose a Target, then press Tune. Help is below.', 0
cs_s_hpick: db 'Pick the chipset above if you know it. Save report writes this page.', 0
cs_s_hxt:   db 'Save report writes this page.', 0
cs_s_tune:  db 'Tune: ', 0
cs_s_tune2: db ' is the fastest that passed', 0
cs_s_tfl:   db '; ', 0
cs_s_tfl2:  db ' read ', 0
cs_s_tfl3:  db ' wrong', 0
cs_s_tnone: db 'none failed down to ', 0
cs_s_ds:    db 'Mfg spec set: ', 0
cs_s_ds2:   db ', the data sheet', 27h, 's value', 0
cs_s_rsv:   db ' - saved', 0
cs_s_rnsv:  db ' - not saved', 0
cs_s_nefind: db 'The NEAT is written only where its probe found it', 0
cs_s_warn:  db 'Untested chipset: may need a reset', 0
cs_s_btxt:  db 'BIOS: ', 0
cs_s_btnone: db 'none found', 0
cs_s_noregs: db 'Registers: none this chipset lets us read', 0
cs_s_neregs: db 'NEAT 60h-6Fh:', 0
cs_s_raw:   db 'Raw PIT counts: RAM ', 0
cs_s_rawe:  db ', empty ', 0
cs_s_rawm:  db ', MUL ', 0
cs_s_rawn:  db ', I/O NOP ', 0
cs_s_rawi:  db ', IN ', 0
cs_s_swno:  db 'Tune sweep: not run', 0
cs_s_swh:   db 'Sweep from ', 0
cs_s_swf:   db ': 3F4h=', 0
cs_s_swnone: db '--', 0
cs_s_swp:   db ' 85h=', 0
cs_s_swyes: db 'ok', 0
cs_s_swno2: db 'no', 0
cs_s_swscr: db ' scr ', 0
cs_s_swrom: db ' rom ', 0
cs_s_swpm:  db ' pm ', 0
cs_s_swok:  db ': ok, ns', 0
cs_s_nscr:  db ' screen ', 0
cs_s_npm:   db ' PicoMEM ', 0
cs_s_nrom:  db ' ROM ', 0
cs_s_nio:   db ' I/O ', 0
cs_s_swfail: db ': FAIL, ', 0
cs_s_swwrong: db ' wrong:', 0
cs_s_wscr:  db ' screen', 0
cs_s_wrom:  db ' ROM', 0
cs_s_wpm:   db ' PicoMEM', 0
cs_s_wramp: db ' 2A3h', 0
cs_s_wfdc:  db ' floppy-status', 0
cs_s_wpage: db ' I/O-write', 0
cs_s_wrb:   db ' read-back', 0
cs_st_stock: db 'Stock: BIOS timing', 0
cs_st_stk:  db 'Stock: ', 0
cs_st_tuned: db 'Tuned: ', 0
cs_st_spec: db 'Mfg spec: ', 0
cs_st_waits: db ' waits', 0
cs_s_saved: db 'Saved: SYSTEM\RLAPPLY.O88 puts it back after each boot', 0
cs_s_svno:  db 'Not saved: no SYSTEM\APPDATA on the boot disk?', 0
cs_s_rpt:   db 'Report written: ', 0
cs_s_rptno: db 'Report not written: the disk refused it', 0
cs_s_conf:  db 'Not detected. Write to it anyway?', 0
cs_s_cancl: db 'Nothing written: you chose Cancel', 0
cs_s_read:  db '8-bit read, ns a byte over RAM: ', 0
cs_s_io:    db '8-bit I/O read (3F4h): ', 0
cs_s_io2:   db ' ns over a NOP', 0
cs_s_io3:   db ' = ', 0
cs_s_ck88:  db 'CPU clock: not timed on an 8088', 0
cs_s_cknone: db 'CPU clock: could not be timed', 0
cs_s_ck:    db 'CPU clock ', 0
cs_s_ck2:   db ' MHz (timed)', 0
cs_s_ckno8: db '; no 8-bit source to time', 0
cs_s_ck3:   db '; 8-bit read ', 0
cs_s_ck4:   db ' clocks', 0
cs_s_ck5:   db ' = R4 ~', 0
cs_s_nomeas: db 'not measured', 0
cs_s_scr:   db 'screen ', 0
cs_s_pm:    db '  PicoMEM ', 0
cs_s_roms:  db 'Option ROMs (seg, KB, ns): ', 0
cs_s_kb:    db 'K ', 0
cs_s_ns:    db 'ns  ', 0
cs_s_probes: db 'Probes (read-only):', 0
cs_s_alias: db '  22h/23h MIRROR THE 8259 - not probed', 0
cs_s_ind:   db '  ', 0
cs_s_read2: db '  read ', 0
cs_s_out:   dw cs_o_nr, cs_o_cpu, cs_o_ali, cs_o_abs, cs_o_fnd, cs_o_pick
cs_o_nr:    db 'not run', 0
cs_o_cpu:   db 'skipped on this CPU class', 0
cs_o_ali:   db 'skipped: 22h/23h are the 8259', 0
cs_o_abs:   db 'absent', 0
cs_o_fnd:   db 'FOUND', 0
cs_o_pick:  db 'no probe - pick it', 0
