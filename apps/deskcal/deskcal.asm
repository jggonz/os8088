; =============================================================================
; os8088 - apps/deskcal/deskcal.asm
;
; THE DESK CALENDAR - the first DESKTOP INTEGRATED APPLICATION (SPEC.md 107,
; its own section 107.3). A month on the desktop that stays where it was put
; and comes back at the next boot: it ships in SYSTEM\DIA\ on the system disk
; and includes apps/os88dia.inc, which owns its place, its state file
; (SYSTEM\APPDATA\DESKCAL.DAT) and its row in the desktop configuration.
;
; Wave 2 is the window, the month, today and the navigation; wave 3 the
; appointments, the tasks, the one-line entry and the Remind menu's lead
; times; the reminders' firing is wave 4 (107.10).
;
;   deskcal.asm   this: header, entry proc, menus, keys, clicks, the edits
;   dcdate.inc    the date maths (day of week, days in a month, the grid)
;   dcdata.inc    the payload: the load's check, the sorted insert, the
;                 deletes, the tasks, the entry's time parser
;   dcdraw.inc    the two layouts and every draw
;
; Two colours on every adapter - ink 0 on ground 15 - so today is INVERSE and
; the selected day is a FRAME: nothing is carried by colour (SPEC.md 39.4).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Desk Calendar', dc_entry, OS88_F_ICON

; A calendar page: a solid header band over a grid of days. It is also the
; DIA row's picture (DIA_ICON below), so a row the boot could not launch
; turns into a desktop icon that looks like what it was (SPEC.md 107.4).
    OS88_ICON16
    dw 0x0000, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x0000, 0x0000
    dw 0x0000, 0x7FFE, 0x6DB6, 0x7FFE, 0x4002, 0x5552, 0x4002, 0x5552
    dw 0x4002, 0x5552, 0x4002, 0x5552, 0x4002, 0x7FFE, 0x0000, 0x0000
    OS88_ICON16_END

; --- geometry (SPEC.md 107.3) ------------------------------------------------
DC_CELLS    equ 26              ; the content is 26 cells wide on both layouts
DC_CW       equ DC_CELLS * 8    ; 208
DC_TALL_CH  equ 236             ; the TALL content; anything shorter is COMPACT
DC_COMP_CH  equ 120
DC_FW       equ DC_CW + 2       ; the frames: content + border, + the title bar
DC_TALL_FH  equ DC_TALL_CH + TITLE_H + 1
DC_COMP_FH  equ DC_COMP_CH + TITLE_H + 1
DC_FLD0     equ 2               ; the first day field's first cell
DC_MARK     equ '*'             ; the mark cell's glyph: a day with an
                                ; appointment. It must be PRINTABLE - the
                                ; kernel keeps glyphs 32..126 and draws any
                                ; other as a space (font.inc), which is what
                                ; wave 2's glyph 7 did: no day ever showed
                                ; its bullet. A star's middle row reaches
                                ; column 0, which the selection frame stands
                                ; on, so a marked day the frame leaves is
                                ; re-run (dc_reselect), as today always was
DC_GRIDN    equ 42

; --- the payload v1, what wave 2 reads of it (SPEC.md 107.3) -----------------
; tools/os88dia.py's DESKCAL_* are the same numbers; t_dia holds them equal
DCP_LEN     equ 2440
DCP_VER     equ 1
DCP_NAPPT   equ 0               ; db: appointments in use
DCP_NTASK   equ 1               ; db: tasks in use
DCP_MAXAPPT equ 48
DCP_MAXTASK equ 32
DCP_APPT0   equ 8               ; the first appointment
DCP_APPTSZ  equ 32
DCP_TASK0   equ DCP_APPT0 + DCP_MAXAPPT * DCP_APPTSZ    ; 1544
DCP_TASKSZ  equ 28
DCA_DATE    equ 0               ; dw: FAT-packed
DCA_TIME    equ 2               ; dw: minute of the day, 0FFFFh all day
DCA_FLAGS   equ 4               ; db: DCA_REMIND, DCA_FIRED
DCA_LEAD    equ 5               ; db: minutes before
DCA_TEXT    equ 6               ; 26 bytes, NUL
DCA_TMAX    equ 25              ; ...so at most 25 characters
DCA_REMIND  equ 1
DCA_FIRED   equ 2
DCT_FLAGS   equ 0               ; db: DCT_DONE, DCT_DATED
DCT_DUE     equ 2               ; dw: FAT-packed, iff DCT_DATED
DCT_TEXT    equ 4               ; 24 bytes, NUL
DCT_TMAX    equ 23
DCT_DONE    equ 1
DCP_LEAD    equ 2               ; db: a new appointment's lead, minutes
DCP_FLAGS   equ 3               ; db: DCF_NOREM
DCF_NOREM   equ 1               ; new appointments get no reminder
DC_ALLDAY   equ 0FFFFh
DC_ESZ      equ 32              ; the entry's buffer, its NUL in it: `HH:MM `
                                ; and 25 characters
DC_TPITCH   equ 12              ; a task row: the check box's height
DC_AMARK    equ 6               ; an agenda line's reminder-mark cell
DCS_APPT    equ 1               ; dc_selk: an appointment selected...
DCS_TASK    equ 2               ; ...or a task
DCE_NEWA    equ 1               ; dc_ed: the field is a new appointment,
DCE_EDA     equ 2               ; an appointment being edited,
DCE_NEWT    equ 3               ; a new task,
DCE_EDT     equ 4               ; a task being edited
%if DCP_TASK0 + DCP_MAXTASK * DCP_TASKSZ != DCP_LEN
  %error "deskcal: the payload's tables do not add up to DCP_LEN"
%endif

KSC_PGUP    equ 0x49
KSC_PGDN    equ 0x51
KSC_DEL     equ 0x53

; --- the DIA (apps/os88dia.inc, SPEC.md 107.5) --------------------------------
%define DIA_DATNAME  'DESKCAL.DAT'
%define DIA_CAP      'Calendar'
%define DIA_ISOPEN   'Desk Calendar is open'
%define DIA_DAMAGEDQ 'Replace damaged calendar?'
%define DIA_ONTICK   dc_tick
DIA_PAYLEN  equ DCP_LEN
DIA_PAYVER  equ DCP_VER
DIA_ICON    equ 32
DIA_BSS     equ DC_BSS          ; the include's state follows ours

; --- bss: OURS FIRST, at fixed offsets - tests read the grid out of a running
; guest (tests/deskcalgrid.py), and tools/os88dia.py's DESKCAL_BSS_* say where
DC_BSS_GRID equ 0               ; 42 bytes, the month on show
DC_BSS_Y    equ 42              ; dw the year on show
DC_BSS_M    equ 44              ; db its month
DC_BSS_SEL  equ 45              ; db the selected day
DC_BSS_TY   equ 46              ; dw today's year
DC_BSS_TM   equ 48              ; db ...month
DC_BSS_TD   equ 49              ; db ...day
dc_grid     equ os88_image_end + DC_BSS_GRID
dc_y        equ os88_image_end + DC_BSS_Y
dc_m        equ os88_image_end + DC_BSS_M
dc_sel      equ os88_image_end + DC_BSS_SEL
dc_ty       equ os88_image_end + DC_BSS_TY
dc_tm       equ os88_image_end + DC_BSS_TM
dc_td       equ os88_image_end + DC_BSS_TD
dc_lay      equ os88_image_end + 50     ; dw the layout table
dc_win      equ os88_image_end + 52     ; dw our window
dc_abon     equ os88_image_end + 54     ; db the About card is up
dc_dow1     equ os88_image_end + 55     ; db the 1st's column
dc_dim      equ os88_image_end + 56     ; db the month's length
dc_mark     equ os88_image_end + 57     ; 4 bytes: bit d = day d has an item
DC_BSS_OX   equ 62                      ; dw the content origin, as the last
DC_BSS_OY   equ 64                      ; draw found it (the rows click by it)
dc_ox       equ os88_image_end + DC_BSS_OX
dc_oy       equ os88_image_end + DC_BSS_OY
dc_cw       equ os88_image_end + 66     ; dw the content box
dc_ch       equ os88_image_end + 68
dc_line     equ os88_image_end + 70     ; 28: a row being built, and its NUL
dc_redo     equ os88_image_end + 98     ; db the next draw is a WHOLE paint: a
                                        ; menu pick took the About card down
DC_BSS_ED   equ 99                      ; db the entry field: 0 shut, DCE_*
DC_BSS_EDI  equ 100                     ; db ...the item it edits
DC_BSS_SELK equ 101                     ; db the selected item: 0, DCS_*
DC_BSS_SELI equ 102                     ; db ...its index in its table
dc_ed       equ os88_image_end + DC_BSS_ED
dc_edi      equ os88_image_end + DC_BSS_EDI
dc_selk     equ os88_image_end + DC_BSS_SELK
dc_seli     equ os88_image_end + DC_BSS_SELI
dc_pane     equ os88_image_end + 103    ; db COMPACT: 0 the agenda, 1 the tasks
dc_atop     equ os88_image_end + 104    ; db the agenda's first item on show
DC_BSS_TTOP equ 105                     ; db ...and the tasks'
DC_BSS_TSHOW equ 107                    ; db task rows occupied on the glass
dc_ttop     equ os88_image_end + DC_BSS_TTOP
dc_ashow    equ os88_image_end + 106    ; db agenda lines occupied on the glass
dc_tshow    equ os88_image_end + DC_BSS_TSHOW
dc_ask      equ os88_image_end + 108    ; db our Delete question is up
dc_delk     equ os88_image_end + 109    ; db ...about this kind
dc_deli     equ os88_image_end + 110    ; db ...and index
dc_afirst   equ os88_image_end + 111    ; db the day's first appointment
dc_acnt     equ os88_image_end + 112    ; db ...and how many
dc_ph       equ os88_image_end + 113    ; db the parser's hour
dc_ln       equ os88_image_end + 114    ; 20: the entry field (os88line.inc)
DC_BSS_EBUF equ 134                     ; 32: its text
dc_ebuf     equ os88_image_end + DC_BSS_EBUF
dc_ck       equ os88_image_end + 166    ; 12: one check box, aimed at a row
dc_rec      equ os88_image_end + 178    ; 32: an appointment being inserted
dc_tq       equ os88_image_end + 210    ; db an edit since the last poll
DC_BSS_ALERT equ 211                    ; db our reminder's alert is up
DC_BSS_NOW  equ 212                     ; dw the minute of the day, as the
DC_BSS_LNOW equ 214                     ; dw last beat read it, and the one the
                                        ; reminders last looked at (0FFFFh none)
dc_alert    equ os88_image_end + DC_BSS_ALERT
dc_now      equ os88_image_end + DC_BSS_NOW
dc_lnow     equ os88_image_end + DC_BSS_LNOW
dc_chirp    equ os88_image_end + 216    ; db the chirp's second note is owed
dc_rtc      equ os88_image_end + 217    ; db OSAPI_CLK_GET's AL: bit 0 an RTC
dc_fresh    equ os88_image_end + 218    ; db dc_cappt: the reminder is armed
                                        ; afresh (new, or its time moved)
dc_ltp      equ os88_image_end + 220    ; dw the LAUNCH's toast, owed to the
                                        ; first beat (0 none): said after the
                                        ; registration's `Settings Saved`, so
                                        ; it is the line left on the bar
DC_BSS_RMSG equ 224                     ; 42: `Reminder HH:MM text`, NUL - the
dc_rmsg     equ os88_image_end + DC_BSS_RMSG    ; alert's line, which it
                                        ; repaints from while it is up; the
                                        ; toast is the same from +9
DC_RMSG_PRE equ 9                       ; `Reminder `
DC_BSS      equ 272                     ; a multiple of 16 (os88dia.inc)
%if 178 + DCP_APPTSZ > 210 || DC_BSS_RMSG + DC_RMSG_PRE + 6 + DCA_TMAX + 1 > DC_BSS || 220 + 2 > DC_BSS_RMSG
  %error "deskcal: its own bss has outgrown DC_BSS"
%endif


; -----------------------------------------------------------------------------
; dc_entry - the entry proc (DS = CS = ours, ES = KERNEL_SEG, the UI task)
; in:  SI = the loader's name, for os88dia.inc
; out: CF = 0 launched; CF = 1 not (another calendar is up, or no window)
; -----------------------------------------------------------------------------
dc_entry:
    push si
    push di
    push si
    call OSAPI_VIDEO            ; DL = the adapter: the TEMPLATE takes its
    pop si                      ; preferred frame BEFORE the create, whose
    cmp dl, VID_CGA             ; wm_fit runs before PREFER can - a 255-row
    jbe .kind                   ; template on a CGA was cut to the band and
    mov dl, VID_VGA             ; its y clamped for a height it never had
.kind:                          ; (SPEC.md 107.3). EGA takes VGA's, as the
    mov bl, dl                  ; kernel does (11.100.1)
    xor bh, bh
    shl bx, 1
    shl bx, 1
    mov ax, [dc_pref + bx]
    mov [dc_tpl + WT_W], ax
    mov ax, [dc_pref + bx + 2]
    mov [dc_tpl + WT_H], ax     ; ...and dia_anchor measures with it too
    mov di, dc_tpl
    call dia_load               ; places the template; CF = 1 one is open
    jc .out
    call dc_check               ; a payload whose counts or order are not
    jnc .ok                     ; ours is a DAMAGED file: empty, and the
    push es                     ; first save asks before it replaces it
    push ds                     ; (SPEC.md 107.3, 107.5.2)
    pop es
    mov di, DIA_PAY
    mov cx, DCP_LEN
    xor al, al
    cld
    rep stosb
    pop es
    or byte [dia_st], DIA_S_DAMAGED
.ok:
    call dc_today               ; today, and the month on show is today's
    mov word [dc_lnow], 0FFFFh  ; no minute seen: the first beat looks
    call dc_missed              ; what came due while we were not running
    mov ax, [dc_ty]
    mov [dc_y], ax
    mov al, [dc_tm]
    mov [dc_m], al
    mov al, [dc_td]
    mov [dc_sel], al
    call dc_build
    call dc_arange              ; the day's appointments
    mov si, dc_tpl
    call OSAPI_WM_CREATE        ; BX = the window, CF = the table is full
    jc .out
    mov [dc_win], bx
    OS88_REGION_MOVABLE
    mov al, 1
    call OSAPI_WM_OWNBG         ; we lay every pixel of the content
    mov al, OSAPI_SAVEU_ON | OSAPI_SAVEU_1BPP
    call OSAPI_WM_SAVEU         ; two colours: a one-plane raise cache
    mov al, 1
    call OSAPI_WM_KEEPH         ; a fixed layout: hang over the dock, never cut
    mov si, dc_pref
    call OSAPI_WM_PREFER        ; TALL, or COMPACT on a CGA
    mov ax, dc_onresize
    call OSAPI_WM_ONRESIZE
    mov si, dc_menus
    call OSAPI_MENU_SET
    mov si, dc_about
    call OSAPI_ABOUT_SET
    mov cx, [es:bx + W_W]       ; the box as PREFER left it: the frame less
    sub cx, 2                   ; its border and its title bar (OSAPI_WM_GEOM
    mov dx, [es:bx + W_H]       ; answers "not visible" until the loader has
    sub dx, TITLE_H + 1         ; shown us)
    call dc_layout
    call dc_menulive            ; the greyed items, as the payload stands
    call dia_made               ; LAST: after KEEPH and PREFER (os88dia.inc)
    clc
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dc_paint - W_PAINT (SI = the window; lock held, the clip armed)
; -----------------------------------------------------------------------------
dc_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_GEOM          ; CX, DX = the content box
    jc .x
    call dc_layout
    call dc_org
    call dc_all
    cmp byte [dc_abon], 0       ; the About card LAST, over the lot, through
    je .x                       ; the _d entry: the region is armed already
    mov bx, [dc_win]
    mov si, dc_ablines
    call os88ui_about_d
.x:
    call dia_painted            ; kern_small: the worker (SPEC.md 107.5.2)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_onresize - the box moved under us (SPEC.md 11.98): SI = the window,
; CX/DX = the new content box. No drawing; the full paint follows
dc_onresize:
    call dc_layout
    push ax                     ; TALL and COMPACT page at different counts:
    mov al, [dc_atop]           ; each list's top re-derived as the page of
    add al, [dc_afirst]         ; the new layout that holds it (SPEC.md 107.3)
    call dc_aview
    mov al, [dc_ttop]
    call dc_tview
    pop ax
    jmp dc_menulive             ; Show Tasks greys on TALL

; -----------------------------------------------------------------------------
; the About card (SPEC.md 12.2, 20.5.1)
; -----------------------------------------------------------------------------
dc_about:
    push bx
    push si
    mov byte [dc_abon], 1
    mov bx, si
    mov si, dc_ablines
    call os88ui_about           ; arms the clip itself
    pop si
    pop bx
    ret

; dc_abdismiss - the card down if it is up, and CF = 1: the event was that
dc_abdismiss:
    cmp byte [dc_abon], 0
    je .no
    mov byte [dc_abon], 0
    call dc_arm
    jc .gone
    call dc_all
.gone:
    stc
    ret
.no:
    clc
    ret

; -----------------------------------------------------------------------------
; navigation - every one is the UI task with the lock held
; -----------------------------------------------------------------------------

; dc_month - a month back (AL = 0FFh) or forward (AL = 1); refused at either
; end of the range. The selected day is kept, or clamped to the new length
dc_month:
    push ax
    push bx
    cmp al, 1
    je .fwd
    call dc_atfirst
    je .x
    dec byte [dc_m]
    jnz .set
    mov byte [dc_m], 12
    dec word [dc_y]
    jmp short .set
.fwd:
    call dc_atlast
    je .x
    inc byte [dc_m]
    cmp byte [dc_m], 12
    jbe .set
    mov byte [dc_m], 1
    inc word [dc_y]
.set:
    mov ax, [dc_y]
    mov bl, [dc_m]
    call dc_dim_of
    cmp [dc_sel], al
    jbe .k
    mov [dc_sel], al
.k:
    call dc_regrid
.x:
    pop bx
    pop ax
    ret

; dc_step - the selection AL days forward (signed), across a month edge;
; refused past either end of the range
dc_step:
    push ax
    cbw
    add al, [dc_sel]            ; AL = the day it lands on, as a signed byte
    cmp al, 1
    jl .back
    cmp al, [dc_dim]
    jg .on
    call dc_select              ; inside the month
    jmp short .x
.back:
    call dc_atfirst
    je .x
    push ax
    mov al, 0FFh
    call dc_month_q             ; the month before, built
    pop ax
    add al, [dc_dim]            ; ...counted back from its end
    jmp short .land
.on:
    call dc_atlast
    je .x
    sub al, [dc_dim]            ; ...counted on from the next one's 1st
    push ax
    mov al, 1
    call dc_month_q
    pop ax
.land:
    mov [dc_sel], al
    call dc_regrid
.x:
    pop ax
    ret

; dc_month_q - dc_month's arithmetic without its draw (AL = 0FFh / 1)
dc_month_q:
    cmp al, 1
    je .fwd
    dec byte [dc_m]
    jnz .b
    mov byte [dc_m], 12
    dec word [dc_y]
    jmp short .b
.fwd:
    inc byte [dc_m]
    cmp byte [dc_m], 12
    jbe .b
    mov byte [dc_m], 1
    inc word [dc_y]
.b:
    jmp dc_build

; dc_select - select day AL of the month on show
dc_select:
    cmp al, [dc_sel]
    je .x
    push ax
    call dc_sidx                ; AL = the old index
    pop bx
    mov [dc_sel], bl
    call dc_reselect
.x:
    ret

; dc_gotoday - today, on its month
dc_gotoday:
    push ax
    mov ax, [dc_ty]
    cmp ax, [dc_y]
    jne .other
    mov al, [dc_tm]
    cmp al, [dc_m]
    jne .other
    mov al, [dc_td]
    call dc_select
    jmp short .x
.other:
    mov ax, [dc_ty]
    mov [dc_y], ax
    mov al, [dc_tm]
    mov [dc_m], al
    mov al, [dc_td]
    mov [dc_sel], al
    call dc_regrid
.x:
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_onkey - W_ONKEY (AL = ASCII, AH = scancode, SI = the window; lock held).
; With the entry field open every key is the field's (os88line_key) and only
; Enter and Esc come back (SPEC.md 107.3)
; -----------------------------------------------------------------------------
dc_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    call dc_abdismiss           ; a key takes the card down and is spent
    jc .x
    cmp byte [dc_ed], 0
    je .cal
    call dc_fkey
    jmp .x
.cal:
    cmp ah, KSC_DEL             ; Del - and not the keypad's `.`, which is
    jne .k1                     ; the same scancode with an ASCII
    or al, al
    jz .del
    cmp al, 0E0h
    je .del
.k1:
    cmp ah, KSC_LEFT
    je .l
    cmp ah, KSC_RIGHT
    je .r
    cmp ah, KSC_UP
    je .u
    cmp ah, KSC_DOWN
    je .d
    cmp ah, KSC_PGUP
    je .pu
    cmp ah, KSC_PGDN
    je .pd
    cmp al, 13
    je .enter
    cmp al, 27
    je .esc
    cmp al, 9
    je .tab
    cmp al, ' '
    je .space
    or al, 0x20                 ; letters: either case
    cmp al, 't'
    je .t
    cmp al, 'n'
    je .na
    cmp al, 'k'
    je .nt
    cmp al, ','                 ; '<' and '>', shifted or not
    je .pu
    cmp al, '<' | 0x20
    je .pu
    cmp al, '.'
    je .pd
    cmp al, '>' | 0x20
    je .pd
    jmp short .x
.l:
    mov al, 0FFh
    jmp short .st
.r:
    mov al, 1
    jmp short .st
.u:
    mov al, -7
    jmp short .st
.d:
    mov al, 7
.st:
    call dc_step
    jmp short .x
.pu:
    mov al, 0FFh
    call dc_month
    jmp short .x
.pd:
    mov al, 1
    call dc_month
    jmp short .x
.t:
    call dc_gotoday
    jmp short .x
.na:
    call dc_newa
    jmp short .x
.nt:
    call dc_newt
    jmp short .x
.enter:
    call dc_edit
    jmp short .x
.del:
    call dc_askdel
    jmp short .x
.esc:
    xor bx, bx                  ; nothing selected
    call dc_selset
    jmp short .x
.tab:
    call dc_pswap
    jmp short .x
.space:
    cmp byte [dc_selk], DCS_TASK
    jne .x
    mov al, [dc_seli]
    call dc_toggle
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_fkey - key AL / AH while the field is open: the field's, or Enter / Esc
dc_fkey:
    mov si, dc_ln
    push word [si + LN_VIEW]    ; what os88line_edit compares against
    push word [si + LN_LEN]
    push word [si + LN_CAR]
    call os88line_key           ; CF = 0 it used the key (flags only)
    pop dx
    pop bx
    pop cx
    jc .key
    mov ax, cx
    call dc_arm
    jc .x                       ; not on the glass: the state is enough
    call dc_frect
    call os88line_edit          ; the cell it changed, and the caret
.x:
    ret
.key:
    cmp al, 13
    je dc_fenter
    cmp al, 27
    je dc_fclose
    ret

; -----------------------------------------------------------------------------
; dc_onclick - W_ONCLICK (CX, DX = the screen point, SI = the window; lock
; held). A day selects; the header's end cells step the month; under the rule
; the lists (SPEC.md 107.3). SI goes back as it came (SPEC.md 13.7)
; -----------------------------------------------------------------------------
dc_onclick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    call dc_abdismiss
    jc .x
    call dc_org
    cmp byte [dc_ed], 0         ; the field open: a press in it places the
    je .cal                     ; caret, and anything else waits
    call dc_arm
    jc .x
    call dc_frect
    mov si, dc_ln
    mov bx, [si + LN_CAR]       ; the bar's old cell
    call os88line_click
    jc .x                       ; outside the field
    mov ax, [si + LN_CAR]
    cmp ax, bx
    je .x                       ; the same cell: nothing moved
    xchg ax, bx
    call os88line_caroff        ; ...and the BAR follows it: the old cell
    xchg ax, bx                 ; repainted, the new bar drawn (the DOS box's
    call os88line_caron         ; dos_caret_to, SPEC.md 107.3)
    jmp short .x
.cal:
    push cx                     ; the screen point, for a check box's hit
    push dx
    sub cx, [dc_ox]             ; content-relative
    sub dx, [dc_oy]
    js .xp
    mov di, [dc_lay]
    mov ax, cx
    mov cl, 3
    shr ax, cl                  ; AX = the cell
    cmp dx, [di + DCL_RULE]
    jae .lists
    mov bx, dx
    sub bx, [di + DCL_HDR]
    cmp bx, 8
    jae .grid
    or ax, ax
    jz .prev
    cmp ax, DC_CELLS - 1
    jne .xp
    mov al, 1
    call dc_month
    jmp short .xp
.prev:
    mov al, 0FFh
    call dc_month
    jmp short .xp
.grid:
    sub ax, DC_FLD0             ; the cell among the fields
    jb .xp
    cmp ax, 21
    jae .xp
    mov bl, 3
    div bl                      ; AL = the column
    mov cl, al
    mov ax, dx
    sub ax, [di + DCL_W0]
    jb .xp
    mov bl, [di + DCL_PITCH]
    div bl                      ; AL = the row
    cmp al, 6
    jae .xp
    mov bl, 7
    mul bl
    add al, cl                  ; the grid index
    mov bl, al
    xor bh, bh
    mov al, [dc_grid + bx]
    or al, al
    jz .xp                      ; a blank field: before the 1st, after the last
    call dc_select
.xp:
    pop dx
    pop cx
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.lists:                         ; AX = the cell, DX = the row (relative)
    mov bx, dx
    sub bx, [di + DCL_HEAD]
    cmp bx, 8
    jae .ag
    call dc_tsolo               ; the row under the rule: COMPACT's swap
    jnc .swap
    cmp word [di + DCL_SPLIT], 0
    jne .xp
.swap:
    call dc_pswap
    jmp short .xp
.ag:
    call dc_aon
    jc .tk
    mov bx, dx
    sub bx, [di + DCL_PANE]
    jb .tk
    mov cl, 3
    shr bx, cl                  ; BX = the agenda line
    cmp bx, [di + DCL_AN]
    jae .tk
    mov ah, al                  ; AH = the cell
    mov al, bl
    call dc_aclick
    jmp short .xp
.tk:
    call dc_ton
    jc .xp
    mov bx, dx
    sub bx, [di + DCL_T0]
    jb .xp
    xchg ax, bx
    mov cl, DC_TPITCH
    div cl                      ; AL = the task row
    cmp al, [di + DCL_TN]
    jae .xp
    pop dx                      ; the screen point back, for the box
    pop cx
    push cx
    push dx
    call dc_tclick
    jmp short .xp

; dc_aclick - a press on agenda line AL, cell AH: the pager pages, an
; appointment is selected - and its reminder flips when the press is on its
; mark - and a blank line selects nothing
dc_aclick:
    push ax
    push bx
    push cx
    push si
    mov ch, ah                  ; CH = the cell
    call dc_apage               ; BL = a page, BH = 1 there are pages
    cmp al, bl
    jb .item
    or bh, bh
    jz .none
    mov al, [dc_atop]           ; the pager: the next page, or the first
    add al, bl
    cmp al, [dc_acnt]
    jb .pg
    xor al, al
.pg:
    mov [dc_atop], al
    call dc_arm
    jc .x
    call dc_agnew
    jmp short .x
.item:
    add al, [dc_atop]
    cmp al, [dc_acnt]
    jae .none
    add al, [dc_afirst]         ; AL = the appointment
    cmp ch, DC_AMARK
    jne .sel
    call dc_busy
    jc .sel
    call dc_aptr
    cmp word [si + DCA_TIME], DC_ALLDAY
    je .sel
    xor byte [si + DCA_FLAGS], DCA_REMIND   ; the reminder, on or off, at
    and byte [si + DCA_FLAGS], ~DCA_FIRED & 0FFh ; its own lead
    call dc_stale               ; ...fired at once if its moment is past
    call dc_touch
.sel:
    mov bl, DCS_APPT
    mov bh, al
    call dc_selset
    jmp short .x
.none:
    xor bx, bx
    call dc_selset
.x:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; dc_tclick - a press on task row AL at the screen point CX, DX: the pager
; pages; on a task, a press on its BOX (os88ui_bhit on the box's rect)
; toggles it - dc_toggle, which fills the mark and nothing else - and
; anywhere else selects it
dc_tclick:
    push ax
    push bx
    push si
    call dc_titem               ; AL = the task, or BH = the pager
    jnc .item
    or bh, bh
    jz .none
    mov al, [dc_ttop]
    add al, bl
    cmp al, [DIA_PAY + DCP_NTASK]
    jb .pg
    xor al, al
.pg:
    mov [dc_ttop], al
    call dc_arm
    jc .x
    xor al, al
    call dc_tasks
    jmp short .x
.item:
    call dc_busy
    jc .sel
    push ax
    sub al, [dc_ttop]           ; the row again
    call dc_ckaim               ; the box's rect (dc_org is current)
    mov bx, dc_ck
    call os88ui_bhit            ; CF = 0 the press is on the box
    pop ax
    jc .sel
    call dc_toggle
    jmp short .x
.sel:
    mov bl, DCS_TASK
    mov bh, al
    call dc_selset
    jmp short .x
.none:
    xor bx, bx
    call dc_selset
.x:
    pop si
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_oncmd - AM_ONCMD (AL = the item, AH = the menu; lock held, no clip). A
; pick closes the entry field as Esc does, first (SPEC.md 107.3)
; -----------------------------------------------------------------------------
dc_oncmd:
    push ax
    push bx
    cmp byte [dc_abon], 0       ; a pick takes the card down BY its own draw:
    je .go                      ; one whole paint rather than the card's
    mov byte [dc_abon], 0       ; repaint and then the month over it (SPEC.md
    mov byte [dc_redo], 1       ; 107.3) - and a field under it goes with it,
    mov byte [dc_ed], 0         ; unseen
    mov byte [dc_ln + LN_FOCUS], 0
    call dc_menulive
.go:
    cmp byte [dc_ed], 0
    je .shut
    call dc_fclose
.shut:
    cmp ah, 1
    je .remind
    cmp al, DCM_TODAY
    jae .month                  ; the three that draw a month themselves
    cmp byte [dc_redo], 0       ; the rest draw onto the content as it is:
    je .cmd                     ; the card's ground first
    mov byte [dc_redo], 0
    call dc_arm
    jc .cmd
    call dc_all
.cmd:
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [dc_cmds + bx]
    jmp short .x
.remind:
    call dc_redo1
    call dc_remind
    jmp short .x
.month:
    je .today
    cmp al, DCM_PREV
    je .prev
    mov al, 1
    call dc_month
    jmp short .done
.prev:
    mov al, 0FFh
    call dc_month
    jmp short .done
.today:
    call dc_gotoday
.done:
    cmp byte [dc_redo], 0       ; the pick drew nothing - refused at an end
    je .x                       ; of the range, or today selected already -
    call dc_redo1               ; and the card's ground is still owed
.x:
    pop bx
    pop ax
    ret

; dc_redo1 - the whole content now if a pick took the About card down
dc_redo1:
    cmp byte [dc_redo], 0
    je .x
    mov byte [dc_redo], 0
    call dc_arm
    jc .x
    call dc_all
.x:
    ret

dc_cmds:    dw dc_newa, dc_newt, dc_edit, dc_askdel, dc_clear, dc_pswap

; -----------------------------------------------------------------------------
; the edits. Every one is the UI task with the lock held, and each draws what
; it changed through dc_arm (nothing when the window is covered: the next
; paint shows the state as it is)
; -----------------------------------------------------------------------------

; dc_touch - an edit: a save owed (dia_touch), held to the next quiet poll
; (dc_tick). Every register and the flags kept
dc_touch:
    mov byte [dc_tq], 1
    jmp dia_touch

; dc_busy - CF = 1 nothing may be changed now: the field is open, or our
; Delete question is up (its answer deletes the item it asked about)
dc_busy:
    cmp byte [dc_ed], 0
    jne .y
    cmp byte [dc_ask], 0
    jne .y
    clc
    ret
.y:
    stc
    ret

; dc_toast - our line SI on the bar (<= 24 characters, SPEC.md 59.10)
dc_toast:
    push cx
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
    ret

; dc_menulive - the items that state a fact (SPEC.md 47): live, or their
; MENU_DIS twin saying why. A store into the item array - the kernel reads it
; when the menu drops (12.2), so nothing is re-registered. Keeps everything
dc_menulive:
    pushf
    push ax
    push di
    mov ax, dc_i_newa
    cmp byte [DIA_PAY + DCP_NAPPT], DCP_MAXAPPT
    jb .a
    mov ax, dc_i_newa0
.a:
    mov [dc_i_cal + 2 * DCM_NEWA], ax
    mov ax, dc_i_newt
    cmp byte [DIA_PAY + DCP_NTASK], DCP_MAXTASK
    jb .t
    mov ax, dc_i_newt0
.t:
    mov [dc_i_cal + 2 * DCM_NEWT], ax
    mov ax, dc_i_edit
    mov di, dc_i_del
    cmp byte [dc_selk], 0
    jne .e
    mov ax, dc_i_edit0
    mov di, dc_i_del0
.e:
    mov [dc_i_cal + 2 * DCM_EDIT], ax
    mov [dc_i_cal + 2 * DCM_DEL], di
    cmp byte [dc_ed], 0         ; the field open: the three that would close
    je .ne                      ; it, throwing its line away, and open
    mov word [dc_i_cal + 2 * DCM_NEWA], dc_i_newa1  ; another over the band
    mov word [dc_i_cal + 2 * DCM_NEWT], dc_i_newt1  ; that close just drew
    mov word [dc_i_cal + 2 * DCM_EDIT], dc_i_edit1  ; (SPEC.md 107.3)
.ne:
    mov ax, dc_i_clr
    call dc_anydone
    jnc .c
    mov ax, dc_i_clr0
.c:
    mov [dc_i_cal + 2 * DCM_CLEAR], ax
    mov di, [dc_lay]
    mov ax, dc_i_pane0
    cmp word [di + DCL_SPLIT], 0
    jne .p
    mov ax, dc_i_showt
    cmp byte [dc_pane], 0
    je .p
    mov ax, dc_i_showd
.p:
    mov [dc_i_cal + 2 * DCM_PANE], ax
    pop di
    pop ax
    popf
    ret

; dc_selset - select BL = 0 (nothing), DCS_APPT or DCS_TASK, index BH: the
; old item's line drawn plain and the new one's inverse, where they are on
; show. Every register kept
dc_selset:
    push ax
    push bx
    mov al, [dc_selk]
    mov ah, [dc_seli]
    mov [dc_selk], bl
    mov [dc_seli], bh
    call dc_menulive
    call dc_arm
    jc .x
    cmp ax, bx
    je .new                     ; the same item: drawn once
    push bx
    mov bx, ax
    call dc_selitem
    pop bx
.new:
    call dc_selitem
.x:
    pop bx
    pop ax
    ret

; dc_selitem - redraw item BL / BH where it is on show (the clip armed)
dc_selitem:
    push ax
    push bx
    mov al, bh
    cmp bl, DCS_APPT
    je .appt
    cmp bl, DCS_TASK
    jne .x
    call dc_ton
    jc .x
    sub al, [dc_ttop]
    jb .x
    push bx
    call dc_tpage
    cmp al, bl
    pop bx
    jae .x
    call dc_ttext
    jmp short .x
.appt:
    call dc_aon
    jc .x
    sub al, [dc_afirst]
    jb .x
    cmp al, [dc_acnt]
    jae .x
    sub al, [dc_atop]
    jb .x
    push bx
    call dc_apage
    cmp al, bl
    pop bx
    jae .x
    call dc_aline
.x:
    pop bx
    pop ax
    ret

; dc_pswap - COMPACT: the other list (Tab, a click on its heading, the menu).
; The heading and the pane under it; nothing on TALL, which shows both
dc_pswap:
    push di
    mov di, [dc_lay]
    cmp word [di + DCL_SPLIT], 0
    jne .x
    xor byte [dc_pane], 1
    call dc_menulive
    call dc_arm
    jc .x
    call dc_day
    call dc_plist
.x:
    pop di
    ret

; dc_plist - COMPACT's one list, whole, under its heading (the clip armed)
dc_plist:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [dc_pane], 0
    jne .t
    mov ax, 0x0100
    call dc_agenda
    mov di, [dc_lay]
    mov bx, [di + DCL_AN]
    mov cl, 3
    shl bx, cl
    add bx, [di + DCL_PANE]
    mov dx, [dc_ch]
    dec dx
    call dc_fill
    jmp short .x
.t:
    call dc_tpaint
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_newa / dc_newt - the field, empty, for a new appointment on the selected
; day / a new task; refused with the reason at a full table (SPEC.md 107.3)
dc_newa:
    call dc_busy
    jc .x
    cmp byte [DIA_PAY + DCP_NAPPT], DCP_MAXAPPT
    jb .ok
    push si
    mov si, dc_s_afull
    call dc_toast
    pop si
.x:
    ret
.ok:
    push ax
    mov al, DCE_NEWA
    mov byte [dc_ebuf], 0
    call dc_fopen
    pop ax
    ret

dc_newt:
    call dc_busy
    jc .x
    cmp byte [DIA_PAY + DCP_NTASK], DCP_MAXTASK
    jb .ok
    push si
    mov si, dc_s_tfull
    call dc_toast
    pop si
.x:
    ret
.ok:
    push ax
    mov al, DCE_NEWT
    mov byte [dc_ebuf], 0
    call dc_fopen
    pop ax
    ret

; dc_edit - the field holding the selected item (Enter, Edit Item)
dc_edit:
    call dc_busy
    jc .r
    push ax
    push cx
    push si
    push di
    mov al, [dc_seli]
    mov [dc_edi], al
    cmp byte [dc_selk], DCS_APPT
    je .appt
    cmp byte [dc_selk], DCS_TASK
    jne .x
    call dc_tptr                ; a task: its text
    add si, DCT_TEXT
    mov di, dc_ebuf
    mov cx, DCT_TMAX
    call dc_copyz
    mov al, DCE_EDT
    jmp short .open
.appt:
    call dc_aptr                ; an appointment: `HH:MM text`, or the text
    mov di, dc_ebuf
    mov ax, [si + DCA_TIME]
    cmp ax, 24 * 60
    jae .txt
    call dc_tfmt
    mov byte [di], ' '
    inc di
.txt:
    add si, DCA_TEXT
    mov cx, DCA_TMAX
    call dc_copyz
    mov al, DCE_EDA
.open:
    call dc_fopen
.x:
    pop di
    pop si
    pop cx
    pop ax
.r:
    ret

; dc_copyz - at most CX characters of the NUL string SI to DI, and a NUL
dc_copyz:
    push ax
    push cx
    push si
    push di
    cld
.l:
    lodsb
    or al, al
    jz .z
    mov [di], al
    inc di
    loop .l
.z:
    mov byte [di], 0
    pop di
    pop si
    pop cx
    pop ax
    ret

; dc_fopen - open the field, mode AL (DCE_*), holding what dc_ebuf holds: in
; its heading's place, on COMPACT with that heading's list on show
dc_fopen:
    push ax
    push cx
    push di
    mov [dc_ed], al
    call dc_menulive            ; the three that would close it grey
    mov word [dc_ln + LN_BUF], dc_ebuf
    mov word [dc_ln + LN_MAX], DC_ESZ
    cmp al, DCE_NEWT
    jb .m
    mov word [dc_ln + LN_MAX], DCT_TMAX + 1
.m:
    mov byte [dc_ln + LN_FOCUS], 1
    mov byte [dc_ln + LN_DIS], 0
    push si
    mov si, dc_ln
    call os88line_resync        ; the caret at the end of what is there
    pop si
    xor ah, ah                  ; AH = the list the field belongs to
    cmp al, DCE_NEWT
    jb .l
    inc ah
.l:
    xor cl, cl                  ; CL = 1: COMPACT, and the other list
    mov di, [dc_lay]
    cmp word [di + DCL_SPLIT], 0
    jne .a
    cmp [dc_pane], ah
    je .a
    mov [dc_pane], ah
    call dc_menulive
    inc cx
.a:
    call dc_arm
    jc .x
    or cl, cl
    jz .d
    call dc_plist               ; that list, then the field over its heading
.d:                             ; - the heading's own run is not drawn first
    call dc_fdraw
.x:
    pop di
    pop cx
    pop ax
    ret

; dc_fclose - shut the field (Esc, an empty Enter, a menu pick, a commit):
; its heading's run back, and the two gap rows it took
dc_fclose:
    push ax
    push bx
    push dx
    push si
    push di
    call dc_fhead               ; AX = its heading's row - asked while open
    mov byte [dc_ed], 0
    mov byte [dc_ln + LN_FOCUS], 0
    call dc_menulive            ; ...and are live again
    call dc_arm
    jc .x
    mov di, [dc_lay]
    mov bx, ax
    sub bx, 3                   ; the frame's top row...
    cmp bx, [di + DCL_RULE]
    ja .t
    mov bx, [di + DCL_RULE]     ; ...unless it IS the rule (COMPACT)
    inc bx
.t:
    mov dx, ax
    dec dx
    call dc_fill
    mov dx, ax
    cmp ax, [di + DCL_HEAD]
    jne .tk
    call dc_day
    jmp short .b
.tk:
    call dc_tasksh
.b:
    add dx, 8                   ; the frame's bottom row
    mov bx, dx
    call dc_fill
.x:
    pop di
    pop si
    pop dx
    pop bx
    pop ax
    ret

; dc_fenter - Enter in the field: keep what it says
dc_fenter:
    cmp byte [dc_ed], DCE_NEWT
    jb dc_cappt
    jmp dc_ctask

; dc_cappt - the field's line as an appointment on the selected day (SPEC.md
; 107.3): parsed, refused when its text is too long, inserted in order -
; an edited one taken out first, keeping its reminder while it stays timed
dc_cappt:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    mov si, dc_ebuf
    call dc_parse               ; BX = the time, SI = the text, CX = its length
    cmp bx, DC_ALLDAY
    jne .some
    or cx, cx
    jnz .some
    jmp .shut                   ; nothing typed: as Esc
.some:
    cmp cx, DCA_TMAX
    jbe .fits
    mov si, dc_s_long
    call dc_toast               ; the field stays open: never cut it - but
    mov si, dc_ln               ; the parse's trim may have NULed trailing
    call os88line_resync        ; spaces, so the field learns its length
    call dc_arm                 ; again and is drawn whole
    jc .x0
    call dc_fdraw
.x0:
    jmp .x
.fits:
    mov byte [dc_fresh], 1      ; armed afresh, unless an edit keeps its time
    push si                     ; dc_rec := the new appointment
    push cx
    push ds
    pop es
    cld
    mov di, dc_rec
    mov cx, DCP_APPTSZ / 2
    xor ax, ax
    rep stosw
    pop cx
    pop si
    lea di, [dc_rec + DCA_TEXT]
    rep movsb
    call dc_pack
    mov [dc_rec + DCA_DATE], ax
    mov [dc_rec + DCA_TIME], bx
    mov al, [DIA_PAY + DCP_LEAD]
    mov [dc_rec + DCA_LEAD], al
    cmp bx, DC_ALLDAY
    je .fl
    test byte [DIA_PAY + DCP_FLAGS], DCF_NOREM
    jnz .fl
    mov byte [dc_rec + DCA_FLAGS], DCA_REMIND
.fl:
    mov al, [dc_sel]
    call dc_marked              ; ZF = 0: the day had its bullet
    pushf
    cmp byte [dc_ed], DCE_EDA
    jne .ins
    mov al, [dc_edi]            ; EDITED: out first, and its reminder kept
    call dc_aptr                ; while it stays timed (its FIRED only while
    mov dx, [si + DCA_TIME]     ; its time does not move)
    cmp dx, DC_ALLDAY
    je .del
    cmp bx, DC_ALLDAY
    je .del
    mov ah, [si + DCA_FLAGS]
    cmp dx, bx
    jne .moved
    mov byte [dc_fresh], 0      ; the same time: its FIRED stands
    jmp short .same
.moved:
    and ah, ~DCA_FIRED & 0FFh
.same:
    mov [dc_rec + DCA_FLAGS], ah
    mov ah, [si + DCA_LEAD]
    mov [dc_rec + DCA_LEAD], ah
.del:
    call dc_adel
    call dc_fixsel
.ins:
    call dc_ains                ; AL = where it went (a new one was refused
    jc .full                    ; at the table's end before the field opened)
    cmp byte [dc_fresh], 0
    je .kept
    call dc_stale               ; a moment already past: fired, silently
.kept:
    mov dl, [dc_selk]           ; DX = the selection before: a TASK's row is
    mov dh, [dc_seli]           ; drawn plain again (an appointment's line is
    mov byte [dc_selk], DCS_APPT    ; the agenda's, which is redrawn)
    mov [dc_seli], al
    call dc_touch
    call dc_fclose
    call dc_arange
    call dc_aview               ; its page
    call dc_build               ; the day's bullet
    mov al, [dc_sel]
    popf
    call dc_markfix
    call dc_menulive
    call dc_arm
    jc .x
    call dc_agnew
    cmp dl, DCS_TASK
    jne .x
    mov bx, dx
    call dc_selitem
    jmp short .x
.full:
    popf
.shut:
    call dc_fclose
.x:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_ctask - the field's line as a task: new at the list's end, or the
; edited one's text
dc_ctask:
    push ax
    push bx
    push cx
    push si
    mov si, dc_ebuf
    call dc_trim
    jcxz .shut
    mov bl, [dc_ttop]           ; the page before, to know what moved
    mov bh, [dc_ed]             ; ...and the mode, which the close clears
    mov al, [dc_edi]
    cmp byte [dc_ed], DCE_EDT
    je .set
    mov al, [DIA_PAY + DCP_NTASK]
    cmp al, DCP_MAXTASK
    jae .shut
    push si
    call dc_tptr
    mov word [si + DCT_FLAGS], 0
    mov word [si + DCT_DUE], 0
    pop si
    inc byte [DIA_PAY + DCP_NTASK]
.set:
    call dc_tset
    push word [dc_selk]         ; the selection before (SELK, SELI adjacent)
    mov byte [dc_selk], DCS_TASK
    mov [dc_seli], al
    call dc_touch
    call dc_fclose
    call dc_tview               ; its page
    call dc_menulive
    call dc_arm
    pop cx                      ; CL / CH = the old selection
    jc .x
    push bx
    mov bx, cx                  ; ...drawn plain where it is on show, if it
    cmp bx, [dc_selk]           ; was another item
    je .same
    call dc_selitem
.same:
    pop bx
    sub al, [dc_ttop]           ; its row: an edit's text alone, a new one
    cmp bl, [dc_ttop]           ; from there down (the pager is the last
    jne .all                    ; occupied row, so it comes with it) - or
    cmp bh, DCE_EDT             ; the whole list when the page moved
    jne .go
    call dc_ttext
    jmp short .x
.all:
    xor al, al
.go:
    call dc_tasks
    jmp short .x
.shut:
    call dc_fclose
.x:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; dc_aview - [dc_atop] := the page holding appointment AL (of the day)
dc_aview:
    push ax
    push bx
    sub al, [dc_afirst]
    call dc_apage
    xor ah, ah
    or bh, bh
    jz .z
    div bl
    mul bl
    jmp short .s
.z:
    xor al, al
.s:
    mov [dc_atop], al
    pop bx
    pop ax
    ret

; dc_tview - [dc_ttop] := the page holding task AL
dc_tview:
    push ax
    push bx
    call dc_tpage
    xor ah, ah
    or bh, bh
    jz .z
    div bl
    mul bl
    jmp short .s
.z:
    xor al, al
.s:
    mov [dc_ttop], al
    pop bx
    pop ax
    ret

; dc_markfix - day AL of the month on show (0: none), whose bullet the flags
; say it HAD (ZF = 0, from dc_marked before the change): when that changed,
; its field re-run - and the frame again when it is the selected day, the
; run laying the frame's side columns. After dc_build. Keeps every register
dc_markfix:
    push ax
    push bx
    push cx
    pushf
    pop bx                      ; BL bit 6 = ZF before
    or al, al
    jz .x
    mov cl, al
    call dc_marked
    pushf
    pop ax
    xor al, bl
    test al, 40h
    jz .x
    call dc_arm
    jc .x
    mov al, cl
    add al, [dc_dow1]
    dec ax
    call dc_field
    cmp cl, [dc_sel]
    jne .x
    call dc_selon
.x:
    pop cx
    pop bx
    pop ax
    ret

; dc_aday - AL = the day of appointment AL when its date is in the month on
; show, else 0. Every other register kept
dc_aday:
    push bx
    push cx
    push si
    mov ch, ah                  ; AH is kept
    call dc_aptr
    mov bx, [si + DCA_DATE]
    mov ax, [dc_y]              ; the month on show as the date's bits 5..
    sub ax, DC_Y0
    mov cl, 4
    shl ax, cl
    or al, [dc_m]
    mov si, bx
    inc cx                      ; CL = 5
    shr bx, cl
    cmp bx, ax
    mov ax, si
    jne .no
    and al, 31
    jmp short .x
.no:
    xor al, al
.x:
    mov ah, ch
    pop si
    pop cx
    pop bx
    ret

; dc_fixsel - appointment AL has gone: a selection of it is nothing now, and
; one after it is one index down. Keeps everything
dc_fixsel:
    cmp byte [dc_selk], DCS_APPT
    jne .x
    cmp al, [dc_seli]
    ja .x
    jb .dn
    mov byte [dc_selk], 0
    ret
.dn:
    dec byte [dc_seli]
.x:
    ret

; dc_askdel - Del / Delete Item: ask, and let the answer delete (SPEC.md
; 107.3). Nothing changes while the question is up (dc_busy)
dc_askdel:
    call dc_busy
    jc .r
    cmp byte [dc_selk], 0
    je .r
    push ax
    push bx
    push si
    push di
    push es
    mov ax, KERNEL_SEG          ; os88ui_ask reads our title out of the
    mov es, ax                  ; window record
    mov si, dc_s_qappt
    cmp byte [dc_selk], DCS_APPT
    je .q
    mov si, dc_s_qtask
.q:
    mov al, OS88UI_AYESNO
    mov bx, [dc_win]
    mov di, dc_delans
    call os88ui_ask             ; CF = 1: another of ours is up (raised)
    jc .x
    mov byte [dc_ask], 1
    mov al, [dc_selk]
    mov [dc_delk], al
    mov al, [dc_seli]
    mov [dc_deli], al
.x:
    pop es
    pop di
    pop si
    pop bx
    pop ax
.r:
    ret

; dc_delans - the question's answer (AL = 0 Yes; lock held, the alert gone)
dc_delans:
    push ax
    push bx
    mov byte [dc_ask], 0
    or al, al
    jnz .x
    mov al, [dc_deli]
    cmp byte [dc_delk], DCS_APPT
    jne .task
    mov bl, al                  ; BL = the appointment
    call dc_aday                ; AL = its day, if this month is on show (the
    mov bh, al                  ; question may have outlived a move to another
    call dc_marked              ; day: ITS bullet is the one at stake)
    mov al, bl
    pushf
    call dc_adel
    call dc_fixsel
    call dc_touch
    call dc_arange
    mov al, [dc_atop]           ; a page that emptied: the first
    cmp al, [dc_acnt]
    jb .ka
    xor al, al
.ka:
    add al, [dc_afirst]         ; ...and the page holding the top NOW - the
    call dc_aview               ; first, when the delete ended paging (SPEC.md
    call dc_build               ; 107.3). dc_agnew draws from line 0 anyway
    mov al, bh
    popf
    call dc_markfix
    call dc_menulive
    call dc_arm
    jc .x
    call dc_agnew
    jmp short .x
.task:
    mov bl, [dc_ttop]
    call dc_tdel
    cmp byte [dc_selk], DCS_TASK
    jne .kt
    cmp al, [dc_seli]
    ja .kt
    jb .tdn
    mov byte [dc_selk], 0
    jmp short .kt
.tdn:
    dec byte [dc_seli]
.kt:
    call dc_touch
    push ax
    mov al, [dc_ttop]           ; a page that emptied: the first...
    cmp al, [DIA_PAY + DCP_NTASK]
    jb .tk
    xor al, al
.tk:
    call dc_tview               ; ...and the page holding the top NOW: the
    pop ax                      ; first when the delete ended paging, which
                                ; BL != [dc_ttop] below redraws whole
    call dc_menulive
    call dc_arm
    jc .x
    sub al, [dc_ttop]           ; from its row down - or the whole list
    jb .all
    cmp bl, [dc_ttop]
    jne .all
    push bx
    call dc_tpage
    cmp al, bl
    pop bx
    jb .go
.all:
    xor al, al
.go:
    call dc_tasks
.x:
    pop bx
    pop ax
    ret

; dc_toggle - task AL checked or not (Space, a press on its box): the mark
; redrawn, ONE fill and nothing else (SPEC.md 107.3)
dc_toggle:
    call dc_busy
    jc .r
    push ax
    push si
    call dc_tptr
    xor byte [si + DCT_FLAGS], DCT_DONE
    mov ah, [si + DCT_FLAGS]
    and ah, DCT_DONE
    call dc_touch
    call dc_menulive
    sub al, [dc_ttop]
    jb .x
    push bx
    call dc_tpage
    cmp al, bl
    pop bx
    jae .x
    call dc_ton
    jc .x
    call dc_arm
    jc .x
    call dc_tmark
.x:
    pop si
    pop ax
.r:
    ret

; dc_clear - Clear Done Tasks: the checked ones out, the list from its top
dc_clear:
    call dc_busy
    jc .r
    push ax
    call dc_tclear              ; AL = how many went
    or al, al
    jz .x
    cmp byte [dc_selk], DCS_TASK
    jne .k
    mov byte [dc_selk], 0
.k:
    mov byte [dc_ttop], 0
    call dc_touch
    call dc_menulive
    call dc_arm
    jc .x
    xor al, al
    call dc_tasks
.x:
    pop ax
.r:
    ret

; dc_remind - Remind item AL: the selected TIMED appointment's lead (or its
; reminder off), else the default for new ones; the toast says which
dc_remind:
    call dc_busy
    jc .r
    push ax
    push bx
    push si
    push di
    xor ah, ah
    mov bx, ax
    mov ah, [dc_leads + bx]     ; AH = the lead, or 0FFh: no reminder
    shl bx, 1
    mov bx, [dc_rlbl + bx]      ; BX = its words
    mov di, dc_s_def
    cmp byte [dc_selk], DCS_APPT
    jne .def
    mov al, [dc_seli]
    call dc_aptr
    cmp word [si + DCA_TIME], DC_ALLDAY
    je .def
    and byte [si + DCA_FLAGS], ~(DCA_REMIND | DCA_FIRED) & 0FFh
    cmp ah, 0FFh
    je .one
    or byte [si + DCA_FLAGS], DCA_REMIND
    mov [si + DCA_LEAD], ah
.one:
    call dc_stale               ; armed for a moment already past: fired
    push bx
    mov bl, DCS_APPT
    mov bh, al
    call dc_arm
    jc .nd
    call dc_selitem
.nd:
    pop bx
    mov di, dc_s_rem
    jmp short .say
.def:
    cmp ah, 0FFh
    jne .lead
    or byte [DIA_PAY + DCP_FLAGS], DCF_NOREM
    jmp short .say
.lead:
    and byte [DIA_PAY + DCP_FLAGS], ~DCF_NOREM & 0FFh
    mov [DIA_PAY + DCP_LEAD], ah
.say:
    call dc_touch
    push di                     ; the toast: its prefix and the lead's words
    call dc_blank
    pop si
    call dc_puts
    mov si, bx
    call dc_puts
    mov byte [di], 0
    mov si, dc_line
    call dc_toast
    pop di
    pop si
    pop bx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; dc_tick - DIA_ONTICK, every 36 ticks (the UI task, lock held, no clip): a
; new DATE moves today's inverse - the old field and the new, each re-run
; alone when it is on show, and the frame again when either is the selected
; day (a run lays the frame's side columns). The month on show does not
; change with the date, so nothing else is drawn; nor is anything while the
; About card is up - the paint that takes it down shows the new today
; (SPEC.md 107.3). Then the reminders' minute (dcrem.inc), and the second
; note of a chirp one beat after the first.
; out: CF = 1 HOLD the save owed (os88dia.inc): the entry field, our Delete
;      question is open, or an edit landed since the last poll - a save is
;      seconds of disk with the lock held (SPEC.md 107.6), so it waits for a
;      quiet poll and a burst of edits is one save. A reminder's alert does
;      NOT hold it: unanswered, it would hold every edit and drag for as
;      long as it stays up, and lose them all at a Restart
; -----------------------------------------------------------------------------
dc_tick:
    push ax
    push bx
    mov ax, [dc_ltp]            ; the launch's summary (dc_missed), said HERE:
    or ax, ax                   ; the first registration writes SYSTEM.CFG
    jz .ch                      ; on this same beat, before us, and CTRL.DRV
    mov word [dc_ltp], 0        ; says `Settings Saved` for it - from the
    push si                     ; entry proc the line was gone in seconds
    mov si, ax                  ; (SPEC.md 107.3)
    call dc_toast
    pop si
    jmp short .hold             ; ...and nothing else this beat, so no
.ch:                            ; reminder's toast covers it: one due now
    cmp byte [dc_chirp], 0      ; fires on the next, 2 s on
    je .c
    mov byte [dc_chirp], 0      ; the chirp's second note, four ticks after
    push cx                     ; its first (dia_nx)
    push dx
    mov ax, 660
    mov cx, 4
    mov dl, 40h
    call OSAPI_SND_TONE
    pop dx
    pop cx
.c:
    call dc_tidx                ; the old today, if it is on show
    mov bl, al
    call dc_today               ; CF = 1 the date changed
    jnc .min
    mov word [dc_lnow], 0FFFFh  ; a new day: the minute's work for it
    cmp byte [dc_abon], 0
    jne .min
    call dc_tidx
    mov bh, al                  ; BL = the old today's index, BH = the new
    and al, bl                  ; (each 0FFh when not on show; an index is
    cmp al, 0FFh                ; < 42, so the AND is 0FFh only for both)
    je .min
    call dc_arm
    jc .min                     ; not visible: the next paint draws it
    mov al, bl
    call .one
    mov al, bh
    call .one
    call dc_sidx
    cmp al, bl
    je .fr
    cmp al, bh
    jne .min
.fr:
    call dc_selon
.min:
    call dc_rpass               ; the reminders: nothing unless the minute moved
.hold:
    mov al, [dc_tq]             ; an edit this poll...
    mov byte [dc_tq], 0
    or al, [dc_ed]              ; ...the field open, or the Delete
    or al, [dc_ask]             ; question up. NOT a reminder's alert: it
    neg al                      ; may stay up for hours, and a Restart
                                ; closes nothing (107.4). CF = 1 for any
    pop bx
    pop ax
    ret
.one:
    cmp al, 0FFh
    je .r
    call dc_field
.r:
    ret

; --- the window (SPEC.md 107.3) -----------------------------------------------
dc_tpl:
    dw 360, 40, DC_FW, DC_TALL_FH
    dw dc_title, dc_paint, dc_onkey, dc_onclick
    OS88_PREFER dc_pref, DC_FW, DC_TALL_FH, DC_FW, DC_TALL_FH, DC_FW, DC_COMP_FH

dc_title:   db 'Desk Calendar', 0

    OS88_MENUSET dc_menus, dc_title, dc_oncmd   ; AM_NAME NOT 0: menu_relayout's
                                ; instance-name fallback sets the cells'
                                ; segment to the KERNEL's, so a package that
                                ; leaves it 0 gets its menus read out of
                                ; kernel memory (SPEC.md 107.9)
        OS88_MENU dc_m_cal, dc_i_cal, DCM_N
        OS88_MENU dc_m_rem, dc_i_rem, 6
    OS88_MENUSET_END dc_menus
DCM_NEWA    equ 0               ; the Calendar menu's items, in order
DCM_NEWT    equ 1
DCM_EDIT    equ 2
DCM_DEL     equ 3
DCM_CLEAR   equ 4
DCM_PANE    equ 5
DCM_TODAY   equ 6               ; ...and the three that draw a month
DCM_PREV    equ 7
DCM_NEXT    equ 8
DCM_N       equ 9
dc_m_cal:   db 'Calendar', 0
dc_m_rem:   db 'Remind', 0
dc_i_cal:   dw dc_i_newa, dc_i_newt, dc_i_edit0, dc_i_del0, dc_i_clr0
            dw dc_i_pane0, dc_i_today, dc_i_prev, dc_i_next
dc_i_newa:  db 'New Appointment  N', 0  ; each greyable item has its twin:
dc_i_newa0: db MENU_DIS, 'New Appointment (full)', 0   ; the grey says THAT,
dc_i_newt:  db 'New Task  K', 0                        ; the words WHY
dc_i_newt0: db MENU_DIS, 'New Task (full)', 0          ; (SPEC.md 47)
dc_i_edit:  db 'Edit Item  Enter', 0
dc_i_edit0: db MENU_DIS, 'Edit Item (no item)', 0
dc_i_newa1: db MENU_DIS, 'New Appointment (typing)', 0
dc_i_newt1: db MENU_DIS, 'New Task (typing)', 0
dc_i_edit1: db MENU_DIS, 'Edit Item (typing)', 0
dc_i_del:   db 'Delete Item  Del', 0
dc_i_del0:  db MENU_DIS, 'Delete Item (no item)', 0
dc_i_clr:   db 'Clear Done Tasks', 0
dc_i_clr0:  db MENU_DIS, 'Clear Done (none done)', 0
dc_i_showt: db 'Show Tasks  Tab', 0
dc_i_showd: db 'Show Day  Tab', 0
dc_i_pane0: db MENU_DIS, 'Show Tasks (both shown)', 0
dc_i_today: db 'Go to Today  T', 0
dc_i_prev:  db 'Previous Month', 0
dc_i_next:  db 'Next Month', 0
dc_i_rem:   dw dc_r0, dc_r1, dc_r2, dc_r3, dc_r4, dc_r5
dc_r0:      db 'At the Time', 0
dc_r1:      db '5 Min Early', 0
dc_r2:      db '15 Min Early', 0
dc_r3:      db '30 Min Early', 0
dc_r4:      db '1 Hour Early', 0
dc_r5:      db 'No Reminder', 0
dc_leads:   db 0, 5, 15, 30, 60, 0FFh   ; ...their leads; 0FFh none
dc_rlbl:    dw dc_l0, dc_l1, dc_l2, dc_l3, dc_l4, dc_l5
dc_l0:      db 'at the time', 0     ; ...and the toast's words for them
dc_l1:      db '5 min early', 0
dc_l2:      db '15 min early', 0
dc_l3:      db '30 min early', 0
dc_l4:      db '1 hour early', 0
dc_l5:      db 'off', 0
dc_s_rem:   db 'Reminder ', 0       ; the selected appointment's...
dc_s_def:   db 'Default: ', 0       ; ...or a new one's (<= 24 in all)
dc_s_afull: db 'Calendar is full (48)', 0
dc_s_tfull: db 'Task list is full (32)', 0
dc_s_long:  db 'At most 25 letters', 0
dc_s_qappt: db 'Delete this appointment?', 0
dc_s_qtask: db 'Delete this task?', 0

dc_ablines: dw dc_ab1, dc_ab2, dc_ab3, dc_ab4, 0
dc_ab1:     db 'Desk Calendar', 0
dc_ab2:     db 'A desktop widget for os8088', 0
dc_ab3:     db 0
dc_ab4:     db 'SYSTEM\DIA  -  SPEC.md 107', 0

%include "dcdate.inc"
%include "dcdraw.inc"

%include "dcdata.inc"
%include "dcrem.inc"

%define OS88UI_ABOUT
%define OS88UI_ALERT
%define OS88UI_CHK
%include "os88ui.inc"
%include "os88line.inc"
%include "os88dia.inc"

%if OS88LINE_SZ != 20 || OS88UI_CK_SIZE != 12
  %error "deskcal: its bss holds a 20-byte field block and a 12-byte check box"
%endif

    align 16, db 0              ; os88dia.inc's read lands on a paragraph
    OS88_BSS DIA_BSS_END
    OS88_IMAGE_END
