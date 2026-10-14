; =============================================================================
; os8088 - apps/deskcal/deskcal.asm
;
; THE DESK CALENDAR - the first DESKTOP INTEGRATED APPLICATION (SPEC.md 107,
; its own section 107.3). A month on the desktop that stays where it was put
; and comes back at the next boot: it ships in SYSTEM\DIA\ on the system disk
; and includes apps/os88dia.inc, which owns its place, its state file
; (SYSTEM\APPDATA\DESKCAL.DAT) and its row in the desktop configuration.
;
; Wave 2 is the window, the month, today and the navigation; the payload is
; already the whole v1 layout (SPEC.md 107.3), READ for the day marks and
; written back with the window's place. Appointments, tasks and reminders are
; the waves after it (107.10).
;
;   deskcal.asm   this: header, entry proc, menus, keys, clicks, the tick
;   dcdate.inc    the date maths (day of week, days in a month, the grid)
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
DC_MARK     equ 7               ; the mark cell's glyph: a day with an
                                ; appointment. Glyph 7 leaves column 0 clear,
                                ; which the selection frame stands on
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
%if DCP_TASK0 + DCP_MAXTASK * DCP_TASKSZ != DCP_LEN
  %error "deskcal: the payload's tables do not add up to DCP_LEN"
%endif

KSC_PGUP    equ 0x49
KSC_PGDN    equ 0x51

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
dc_ox       equ os88_image_end + 62     ; dw the content origin
dc_oy       equ os88_image_end + 64
dc_cw       equ os88_image_end + 66     ; dw the content box
dc_ch       equ os88_image_end + 68
dc_line     equ os88_image_end + 70     ; 28: a row being built, and its NUL
dc_redo     equ os88_image_end + 98     ; db the next draw is a WHOLE paint: a
                                        ; menu pick took the About card down
DC_BSS      equ 112                     ; a multiple of 16 (os88dia.inc)
%if 98 + 1 > DC_BSS
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
    call dc_today               ; today, and the month on show is today's
    mov ax, [dc_ty]
    mov [dc_y], ax
    mov al, [dc_tm]
    mov [dc_m], al
    mov al, [dc_td]
    mov [dc_sel], al
    call dc_build
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
    ret

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
; dc_onkey - W_ONKEY (AL = ASCII, AH = scancode, SI = the window; lock held)
; -----------------------------------------------------------------------------
dc_onkey:
    push ax
    call dc_abdismiss           ; a key takes the card down and is spent
    jc .x
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
    or al, 0x20                 ; letters: either case
    cmp al, 't'
    je .t
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
.x:
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_onclick - W_ONCLICK (CX, DX = the screen point, SI = the window; lock
; held). A day selects; the header's end cells step the month. SI goes back
; as it came (SPEC.md 13.7)
; -----------------------------------------------------------------------------
dc_onclick:
    push ax
    push bx
    push cx
    push dx
    push di
    call dc_abdismiss
    jc .x
    call dc_org
    sub cx, [dc_ox]             ; content-relative
    sub dx, [dc_oy]
    js .x
    mov di, [dc_lay]
    mov ax, cx
    mov cl, 3
    shr ax, cl                  ; AX = the cell
    mov bx, dx
    sub bx, [di + DCL_HDR]
    cmp bx, 8
    jae .grid
    or ax, ax
    jz .prev
    cmp ax, DC_CELLS - 1
    jne .x
    mov al, 1
    call dc_month
    jmp short .x
.prev:
    mov al, 0FFh
    call dc_month
    jmp short .x
.grid:
    sub ax, DC_FLD0             ; the cell among the fields
    jb .x
    cmp ax, 21
    jae .x
    mov bl, 3
    div bl                      ; AL = the column
    mov cl, al
    mov ax, dx
    sub ax, [di + DCL_W0]
    jb .x
    mov bl, [di + DCL_PITCH]
    div bl                      ; AL = the row
    cmp al, 6
    jae .x
    mov bl, 7
    mul bl
    add al, cl                  ; the grid index
    mov bl, al
    xor bh, bh
    mov al, [dc_grid + bx]
    or al, al
    jz .x                       ; a blank field: before the 1st, after the last
    call dc_select
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_oncmd - AM_ONCMD (AL = the item, AH = the menu; lock held, no clip)
; -----------------------------------------------------------------------------
dc_oncmd:
    push ax
    cmp byte [dc_abon], 0       ; a pick takes the card down BY its own draw:
    je .go                      ; one whole paint rather than the card's
    mov byte [dc_abon], 0       ; repaint and then the month over it (SPEC.md
    mov byte [dc_redo], 1       ; 107.3)
.go:
    or al, al
    je .today
    cmp al, 1
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
    mov byte [dc_redo], 0       ; and the card's ground is still owed
    call dc_arm
    jc .x
    call dc_all
.x:
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_tick - DIA_ONTICK, every 36 ticks (the UI task, lock held, no clip): a
; new DATE moves today's inverse - the old field and the new, each re-run
; alone when it is on show, and the frame again when either is the selected
; day (a run lays the frame's side columns). The month on show does not
; change with the date, so nothing else is drawn; nor is anything while the
; About card is up - the paint that takes it down shows the new today
; (SPEC.md 107.3). The minute's work is wave 4's
; -----------------------------------------------------------------------------
dc_tick:
    push ax
    push bx
    call dc_tidx                ; the old today, if it is on show
    mov bl, al
    call dc_today               ; CF = 1 the date changed
    jnc .x
    cmp byte [dc_abon], 0
    jne .x
    call dc_tidx
    mov bh, al                  ; BL = the old today's index, BH = the new
    and al, bl                  ; (each 0FFh when not on show; an index is
    cmp al, 0FFh                ; < 42, so the AND is 0FFh only for both)
    je .x
    call dc_arm
    jc .x                       ; not visible: the next paint draws it
    mov al, bl
    call .one
    mov al, bh
    call .one
    call dc_sidx
    cmp al, bl
    je .fr
    cmp al, bh
    jne .x
.fr:
    call dc_selon
.x:
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
        OS88_MENU dc_m_cal, dc_i_cal, 3
    OS88_MENUSET_END dc_menus
dc_m_cal:   db 'Calendar', 0
dc_i_cal:   dw dc_i_today, dc_i_prev, dc_i_next
dc_i_today: db 'Go to Today', 0
dc_i_prev:  db 'Previous Month', 0
dc_i_next:  db 'Next Month', 0

dc_ablines: dw dc_ab1, dc_ab2, dc_ab3, dc_ab4, 0
dc_ab1:     db 'Desk Calendar', 0
dc_ab2:     db 'A desktop widget for os8088', 0
dc_ab3:     db 0
dc_ab4:     db 'SYSTEM\DIA  -  SPEC.md 107', 0

%include "dcdate.inc"
%include "dcdraw.inc"

%define OS88UI_ABOUT
%define OS88UI_ALERT
%include "os88ui.inc"
%include "os88dia.inc"

    align 16, db 0              ; os88dia.inc's read lands on a paragraph
    OS88_BSS DIA_BSS_END
    OS88_IMAGE_END
