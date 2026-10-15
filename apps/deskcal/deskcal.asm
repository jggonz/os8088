; =============================================================================
; os8088 - apps/deskcal/deskcal.asm
;
; THE DESK CALENDAR - the first DESKTOP INTEGRATED APPLICATION (SPEC.md 110,
; its own section 110.3). A month on the desktop that stays where it was put
; and comes back at the next boot: it ships in SYSTEM\DIA\ on the system disk
; and includes apps/os88dia.inc, which owns its place, its state file
; (SYSTEM\APPDATA\DESKCAL.DAT) and its row in the desktop configuration.
;
; v2 (wave 4) is a WIDGET: three white cards ON the desk - under every
; application window, chromeless, no dock tile - with the desktop's own
; ground between them and a name tag under them that turns into a handle
; while the pointer is over it (os88dia.inc's DIA_WIDGET, SPEC.md 110.11).
; Card 1 is today's page and the month, card 2 the selected day's
; appointments and the tasks, card 3 the reminder; a CGA's COMPACT layout
; pages cards 2 and 3 through one slot.
;
;   deskcal.asm   this: header, entry proc, menus, keys, the card presses
;                 and releases, the edits, the tick
;   dcdate.inc    the date maths (day of week, days in a month, the grid)
;   dcdata.inc    the payload: the load's check, the sorted insert, the
;                 deletes, the tasks, the entry's time parser
;   dcdraw.inc    the three layouts, the composers, the partial redraws
;   dcrem.inc     the reminders: firing, snooze, dismiss, card 3's state
;
; Two colours on every adapter - ink and paper - so today is INVERSE and
; the selected day an OUTLINE: nothing is carried by colour (SPEC.md 39.4).
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Desk Calendar', dc_entry, OS88_F_ICON | OS88_F_DESK

; A calendar page: a solid header band over a grid of days. It is also the
; DIA row's picture (DIA_ICON below), so a row the boot could not launch
; turns into a desktop icon that looks like what it was (SPEC.md 110.4).
    OS88_ICON16
    dw 0x0000, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x0000, 0x0000
    dw 0x0000, 0x7FFE, 0x6DB6, 0x7FFE, 0x4002, 0x5552, 0x4002, 0x5552
    dw 0x4002, 0x5552, 0x4002, 0x5552, 0x4002, 0x7FFE, 0x0000, 0x0000
    OS88_ICON16_END

DC_GRIDN    equ 42

; --- the payload v1, what the calendar reads of it (SPEC.md 110.3) -----------
; tools/os88dia.py's DESKCAL_* are the same numbers; t_dia holds them equal
DCP_LEN     equ 2440
DCP_VER     equ 1
DCP_NAPPT   equ 0               ; db: appointments in use
DCP_NTASK   equ 1               ; db: tasks in use
DCP_SNZT    equ 4               ; dw: the SNOOZE SLOT's minute of the day (v2)
DCP_SNZD    equ 6               ; dw: ...and its FAT date - read only beside a
                                ; SNOOZED appointment, so v1's zeros mean nothing
DCP_MAXAPPT equ 48
DCP_MAXTASK equ 32
DCP_APPT0   equ 8               ; the first appointment
DCP_APPTSZ  equ 32
DCP_TASK0   equ DCP_APPT0 + DCP_MAXAPPT * DCP_APPTSZ    ; 1544
DCP_TASKSZ  equ 28
DCA_DATE    equ 0               ; dw: FAT-packed
DCA_TIME    equ 2               ; dw: minute of the day, 0FFFFh all day
DCA_FLAGS   equ 4               ; db: DCA_REMIND, FIRED, SNOOZED, DUE
DCA_LEAD    equ 5               ; db: minutes before
DCA_TEXT    equ 6               ; 26 bytes, NUL
DCA_TMAX    equ 25              ; ...so at most 25 characters
DCA_REMIND  equ 1               ; armed
DCA_FIRED   equ 2               ; it went off
DCA_SNOOZED equ 4               ; v2: snoozed - it rings again at the slot
DCA_DUE     equ 8               ; v2: it went off and is NOT DISMISSED: card 3
                                ; rings it. A v1 file has neither, so a v1
                                ; FIRED reminder reads as already dismissed
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
DCS_APPT    equ 1               ; dc_selk: an appointment selected...
DCS_TASK    equ 2               ; ...or a task
DCE_NEWA    equ 1               ; dc_ed: the field is a new appointment,
DCE_EDA     equ 2               ; an appointment being edited,
DCE_NEWT    equ 3               ; a new task,
DCE_EDT     equ 4               ; a task being edited,
DCE_NEW     equ 5               ; ...or either: `+`, the parse decides
%if DCP_TASK0 + DCP_MAXTASK * DCP_TASKSZ != DCP_LEN
  %error "deskcal: the payload's tables do not add up to DCP_LEN"
%endif

KSC_PGUP    equ 0x49
KSC_PGDN    equ 0x51
KSC_DEL     equ 0x53

; --- the DIA (apps/os88dia.inc, SPEC.md 110.5, 110.11) -----------------------
%define DIA_DATNAME  'DESKCAL.DAT'
%define DIA_CAP      'Calendar'
%define DIA_ISOPEN   'Desk Calendar is open'
%define DIA_DAMAGEDQ 'Replace damaged calendar?'
%define DIA_ONTICK   dc_tick
DIA_PAYLEN  equ DCP_LEN
DIA_PAYVER  equ DCP_VER
DIA_ICON    equ 32
DIA_BSS     equ DC_BSS          ; the include's state follows ours
%define DIA_WIDGET
%define DIA_TAG       'Desk Calendar'   ; = the title
%define DIA_RIGHT     264       ; the face opens 264 px in from the right edge
                                ; (a %define: the include tests it with %ifdef)
%define DIA_LAYOUTS   dc_lays
%define DIA_PAINTCARD dc_pcard
%define DIA_COMPOSE   dc_compose
%define DIA_ONCARD    dc_oncard
%define DIA_ONCARDUP  dc_oncardup
%define DIA_ONFOCUS   dc_onfocus
%define GFXE_TEXT
%define GFXE_BITS
%define GFXE_BAND_W   176       ; the face: 22 cells, a 704-byte band
%define GFXE_BAND_H   32
%define GFXE_BAND_BUF dc_band

; --- bss: OURS FIRST, at fixed offsets - tests read it out of a running guest
; (tests/deskcal*.py), and tools/os88dia.py's DESKCAL_BSS_* say where
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
dc_win      equ os88_image_end + 50     ; dw our window
dc_abon     equ os88_image_end + 52     ; db the About card is up
dc_dow1     equ os88_image_end + 53     ; db the 1st's column (Monday first)
dc_dim      equ os88_image_end + 54     ; db the month's length
dc_clip     equ os88_image_end + 55     ; db the clip is armed this lock hold
dc_mark     equ os88_image_end + 56     ; 4 bytes: bit d = day d has an item
dc_geo      equ os88_image_end + 60     ; dw the layout's geometry (dcdraw)
DC_BSS_ED   equ 62                      ; db the entry field: 0 shut, DCE_*
DC_BSS_EDI  equ 63                      ; db ...the item it edits
DC_BSS_SELK equ 64                      ; db the selected item: 0, DCS_*
DC_BSS_SELI equ 65                      ; db ...its index in its table
DC_BSS_PANE equ 66                      ; db COMPACT's slot: 0 the day, 1 the
                                        ; tasks, 2 the reminder
dc_ed       equ os88_image_end + DC_BSS_ED
dc_edi      equ os88_image_end + DC_BSS_EDI
dc_selk     equ os88_image_end + DC_BSS_SELK
dc_seli     equ os88_image_end + DC_BSS_SELI
dc_pane     equ os88_image_end + DC_BSS_PANE
dc_pane0    equ os88_image_end + 67     ; db the page a ringing reminder took
                                        ; the slot from, 0FFh: not taken
DC_BSS_ATOP equ 68                      ; db the agenda's first item on show
DC_BSS_TTOP equ 69                      ; db ...and the tasks'
dc_atop     equ os88_image_end + DC_BSS_ATOP
dc_ttop     equ os88_image_end + DC_BSS_TTOP
dc_ask      equ os88_image_end + 70     ; db our Delete question is up
dc_delk     equ os88_image_end + 71     ; db ...about this kind
dc_deli     equ os88_image_end + 72     ; db ...and index
dc_afirst   equ os88_image_end + 73     ; db the day's first appointment
DC_BSS_ACNT equ 74
dc_acnt     equ os88_image_end + DC_BSS_ACNT    ; db ...and how many
dc_ph       equ os88_image_end + 75     ; db the parser's hour
dc_tq       equ os88_image_end + 76     ; db an edit since the last poll
dc_chirp    equ os88_image_end + 77     ; db the chirp's second note is owed
dc_rtc      equ os88_image_end + 78     ; db OSAPI_CLK_GET's AL: bit 0 an RTC
dc_fresh    equ os88_image_end + 79     ; db dc_cappt: armed afresh
DC_BSS_NOW  equ 80                      ; dw the minute of the day, as the
DC_BSS_LNOW equ 82                      ; dw last beat read it, and the one the
                                        ; reminders last looked at (0FFFFh none)
dc_now      equ os88_image_end + DC_BSS_NOW
dc_lnow     equ os88_image_end + DC_BSS_LNOW
dc_ltp      equ os88_image_end + 84     ; dw the LAUNCH's toast, owed to the
                                        ; first beat (0 none)
DC_BSS_C3K  equ 86                      ; db card 3: DC3_NONE / NEXT / RING
DC_BSS_C3A  equ 87                      ; db ...its appointment
DC_BSS_C3T  equ 88                      ; dw ...the time it shows
dc_c3k      equ os88_image_end + DC_BSS_C3K
dc_c3a      equ os88_image_end + DC_BSS_C3A
dc_c3t      equ os88_image_end + DC_BSS_C3T
DC_BSS_LIT  equ 90                      ; db the HELD button's card + 1, 0 none
dc_lit      equ os88_image_end + DC_BSS_LIT
dc_lact     equ os88_image_end + 91     ; db ...its DCB_*
dc_lx0      equ os88_image_end + 92     ; db ...its first byte of the face
dc_lx1      equ os88_image_end + 93     ; db ...its last
dc_ly0      equ os88_image_end + 94     ; dw ...its first card row
dc_ly1      equ os88_image_end + 96     ; dw ...its last
dc_lpend    equ os88_image_end + 98     ; db its un-light owed (card + 1)
dc_c3f      equ os88_image_end + 99     ; db card 3 redrawn whatever its state
dc_cr0      equ os88_image_end + 100    ; dw dc_compose: the band's first row
dc_cr1      equ os88_image_end + 102    ; dw ...and past its last
dc_ccard    equ os88_image_end + 104    ; db ...the card
dc_tdow     equ os88_image_end + 105    ; db today's day of the week
dc_tix      equ os88_image_end + 106    ; db today's grid index, 0FFh none
dc_six      equ os88_image_end + 107    ; db the selected day's
dc_wrow     equ os88_image_end + 108    ; db the week being composed
dc_wcol     equ os88_image_end + 109    ; db ...its column
dc_wr       equ os88_image_end + 110    ; dw ...its number row
dc_wday     equ os88_image_end + 112    ; db ...the day
dc_wink     equ os88_image_end + 113    ; db ...its ink (0 / GFXE_INV)
dc_wfx      equ os88_image_end + 114    ; dw ...its field's x
dc_li       equ os88_image_end + 116    ; db a list's line
dc_lsec     equ os88_image_end + 117    ; db ...which list: 0 agenda, 1 tasks
dc_tty      equ os88_image_end + 118    ; dw a task's text row below its box
dc_rcard    equ os88_image_end + 120    ; db dc_rrect: the card
dc_hk       equ os88_image_end + 121    ; db dc_hit: the kind, DCH_*
dc_rr0      equ os88_image_end + 122    ; dw dc_rrect: the rows
dc_rr1      equ os88_image_end + 124
dc_rb       equ os88_image_end + 126    ; 2 db: ...the bytes
dc_rtop     equ os88_image_end + 128    ; dw ...the card's screen top
dc_hi       equ os88_image_end + 130    ; db dc_hit: a day, an item
dc_hb       equ os88_image_end + 131    ; db ...a button, DCB_*
dc_hx0      equ os88_image_end + 132    ; db ...its rect's bytes
dc_hx1      equ os88_image_end + 133
dc_hy0      equ os88_image_end + 134    ; dw ...and card rows
dc_hy1      equ os88_image_end + 136
dc_qx       equ os88_image_end + 138    ; dw the press, card-relative
dc_qy       equ os88_image_end + 140
dc_cx0      equ os88_image_end + 142    ; dw dc_rrect's x window: its first
dc_cx1      equ os88_image_end + 144    ; pixel, past its last - 0 none
dc_ln       equ os88_image_end + 160    ; 20: the entry field (os88line.inc)
DC_BSS_EBUF equ 180                     ; 32: its text
dc_ebuf     equ os88_image_end + DC_BSS_EBUF
dc_rec      equ os88_image_end + 212    ; 32: an appointment being inserted
DC_BSS_RMSG equ 244                     ; 42: the toast's line, NUL
dc_rmsg     equ os88_image_end + DC_BSS_RMSG
DC_BSS_LINE equ 288
dc_line     equ os88_image_end + DC_BSS_LINE    ; 32: a string being built
DC_BSS_BAND equ 320
dc_band     equ os88_image_end + DC_BSS_BAND    ; 704: the composer's band,
                                        ; 176 x 32 (os88gfx.inc's GFXE_BAND)
DC_BSS      equ DC_BSS_BAND + 176 / 8 * 32  ; 1024: a multiple of 16
%if 146 > 160 || 160 + 20 > DC_BSS_EBUF || DC_BSS_EBUF + DC_ESZ > 212 || 212 + DCP_APPTSZ > DC_BSS_RMSG || DC_BSS_RMSG + 42 > DC_BSS_LINE || DC_BSS_LINE + 32 > DC_BSS_BAND || DC_BSS % 16
  %error "deskcal: its own bss has outgrown its offsets"
%endif


; -----------------------------------------------------------------------------
; dc_entry - the entry proc (DS = CS = ours, ES = KERNEL_SEG, the UI task)
; in:  SI = the loader's name, for os88dia.inc
; out: CF = 0 launched; CF = 1 not (another calendar is up, or no window)
; -----------------------------------------------------------------------------
dc_entry:
    push si
    push di
    mov di, dc_tpl
    call dia_load               ; QUERY, the layout by the screen's height,
    jc .out                     ; the template's size and place (110.11)
    mov al, [dia_li]
    mov ah, DCG_SZ
    mul ah
    add ax, dc_geos
    mov [dc_geo], ax            ; our own geometry for that layout
    call dc_check               ; a payload whose counts or order are not
    jnc .ok                     ; ours is a DAMAGED file: empty, and the
    push es                     ; first save asks before it replaces it
    push ds                     ; (SPEC.md 110.3, 110.5.2)
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
    mov byte [dc_pane0], 0FFh
    call dc_c3calc              ; card 3 as the file left it - and a CGA's
    cmp byte [dia_li], 2        ; slot opens on the reminder when it rings
    jne .mk
    cmp byte [dc_c3k], DC3_RING
    jne .mk
    mov byte [dc_pane], 2
    mov byte [dc_pane0], 0
.mk:
    mov si, dc_tpl
    call OSAPI_WM_CREATE        ; BX = the window, CF = the table is full
    jc .out
    mov [dc_win], bx
    OS88_REGION_MOVABLE
    mov al, 1
    call OSAPI_WM_KEEPH         ; a fixed layout: hang over the dock, never cut
    mov si, dc_menus
    call OSAPI_MENU_SET
    mov si, dc_about
    call OSAPI_ABOUT_SET
    call dc_menulive            ; the greyed items, as the payload stands
    call dia_made               ; LAST: MAKE, the prefilters, the tick
    clc
.out:
    pop di
    pop si
    ret

; -----------------------------------------------------------------------------
; dc_paint - W_PAINT (SI = the window; lock held, the clip armed): the
; include's paint - the ground, every card's slabs, the tag - then the About
; card over it while it is up
; -----------------------------------------------------------------------------
dc_paint:
    mov byte [dc_clip], 1
    call dia_paint
    cmp byte [dc_abon], 0
    je .x
    push bx
    push si
    mov bx, [dc_win]
    mov si, dc_ablines
    call os88ui_about_d
    pop si
    pop bx
.x:
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

; dc_abdismiss - the card down if it is up - what it covered again, ONE
; repaint over its rows (os88dia.inc's dia_redrawr: the slabs those rows
; reach, the content's width) - and CF = 1: the event was that. The rows are
; os88ui_about_d's: the content's height less the card's, halved
DC_ABN      equ 4               ; dc_ablines' lines
DC_ABH      equ OS88UI_ABPADY * 2 + DC_ABN * OS88UI_ABLH
dc_abdismiss:
    cmp byte [dc_abon], 0
    je .no
    mov byte [dc_abon], 0
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, [dc_win]
    call OSAPI_WM_CONTENT       ; AX = the content's left, DX its top
    push dx
    push ax
    call OSAPI_WM_GEOM          ; CX, DX = its size
    pop ax
    pop bx                      ; BX = its top (a pop writes no flags)
    jc .x                       ; not on the glass: nothing to repaint
    push cx
    mov cx, DC_ABH
    cmp cx, dx
    jbe .h
    mov cx, dx                  ; clamped as the card is
.h:
    sub dx, cx
    shr dx, 1
    add bx, dx                  ; BX = the card's top
    mov dx, bx
    add dx, cx
    dec dx                      ; DX = its last row
    pop cx
    add cx, ax
    dec cx                      ; CX = the content's last column
    mov si, [dc_win]
    call dia_redrawr
.x:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
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

; dc_newday - the selected day changed: its appointments, from their first;
; an appointment selected on the old day is selected no more
dc_newday:
    call dc_arange
    mov byte [dc_atop], 0
    cmp byte [dc_selk], DCS_APPT
    jne .x
    mov byte [dc_selk], 0
.x:
    jmp dc_menulive

; dc_select - select day AL of the month on show: the old day's box and the
; new day's, a sub-rect each, and card 2 (SPEC.md 110.3: <= 5 calls)
dc_select:
    cmp al, [dc_sel]
    je .r
    push ax
    push bx
    push ax
    call dc_sidx
    mov bl, al                  ; BL = the old index
    pop ax
    mov [dc_sel], al
    call dc_newday
    call dc_arm
    jc .x
    call dc_tidx
    mov bh, al                  ; BH = today's: its box is inverse selected
    mov al, bl                  ; or not, so it is never drawn again here
    cmp al, bh
    je .n1
    call dc_rbox
.n1:
    call dc_sidx
    cmp al, bh
    je .n2
    call dc_rbox
.n2:
    call dc_rday
.x:
    pop bx
    pop ax
.r:
    ret

; dc_regrid - a new month on show: the grid, the day's list, card 1 from the
; nav down and card 2 (SPEC.md 110.3: <= 8 calls)
dc_regrid:
    call dc_build
    call dc_newday
    call dc_arm
    jc .x
    call dc_rmon
    call dc_rday
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

; --- the redraws the edits use (the clip armed by dc_arm) --------------------

; dc_rday - card 2 whole (the slot on COMPACT)
dc_rday:
    push ax
    mov al, 1
    call dc_rcardw
    pop ax
    ret

; dc_rmon - card 1 from the nav down: a new month on show
dc_rmon:
    push ax
    push bx
    push dx
    push di
    mov di, [dc_geo]
    mov bl, [di + DCG_NAVY]
    xor bh, bh
    sub bx, 2
    xor al, al
    call dc_chgt
    dec dx
    call dc_rrows
    pop di
    pop dx
    pop bx
    pop ax
    ret

; dc_rhdr - card 1's header block: today's page, a new date
dc_rhdr:
    push ax
    push bx
    push dx
    push di
    mov di, [dc_geo]
    mov dl, [di + DCG_HDRB]
    xor dh, dh
    xor bx, bx
    xor al, al
    call dc_rrows
    pop di
    pop dx
    pop bx
    pop ax
    ret

; dc_rbox - grid index AL's box alone (0FFh: nothing), a sub-rect
dc_rbox:
    cmp al, 0FFh
    je .r
    push ax
    push bx
    push cx
    push dx
    push di
    mov di, [dc_geo]
    xor ah, ah
    mov bl, 7
    div bl                      ; AL = the week, AH = the column
    mov cl, ah
    mul byte [di + DCG_WP]
    add al, [di + DCG_W0]
    adc ah, 0
    mov bl, [di + DCG_BOXT]
    xor bh, bh
    sub ax, bx
    mov bx, ax                  ; BX = the box's first row
    mov dl, [di + DCG_BOXH]
    xor dh, dh
    add dx, bx
    dec dx
    mov al, cl
    mov ah, DC_FLDW
    mul ah
    add ax, DC_FLD0 + 2         ; the box's first pixel...
    mov cx, ax
    add ax, DC_BOXW - 1         ; ...and its last
    push cx
    mov cl, 3
    shr ax, cl
    mov ch, al
    pop ax
    shr ax, cl
    mov cl, al
    xor al, al
    call dc_rrect
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; dc_rtbox - task AL's box alone, when it is on show
dc_rtbox:
    push ax
    push bx
    push cx
    push dx
    push di
    cmp byte [dia_li], 2
    jne .on
    cmp byte [dc_pane], 1
    jne .x                      ; COMPACT, and the tasks are not the page
.on:
    sub al, [dc_ttop]
    jb .x
    push ax
    call dc_tslot               ; is the row an item (and not the pager)?
    pop ax
    jc .x
    call dc_trow                ; DX = the box's top
    mov bx, dx
    mov di, [dc_geo]
    mov al, [di + DCG_BOX]
    xor ah, ah
    add dx, ax
    dec dx
    add ax, 7                   ; its bytes: from x 8
    mov ch, al
    shr ch, 1
    shr ch, 1
    shr ch, 1
    mov cl, 1
    mov al, 1
    call dc_rrect
.x:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dc_onkey - W_ONKEY (AL = ASCII, AH = scancode, SI = the window; lock held).
; With the entry field open every key is the field's (os88line_key) and only
; Enter and Esc come back (SPEC.md 110.3)
; -----------------------------------------------------------------------------
dc_onkey:
    push ax
    push bx
    push cx
    push dx
    push si
    mov byte [dc_clip], 0
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
    cmp al, 's'
    je .snz
    cmp al, 'd'
    je .dis
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
.snz:
    call dc_snooze
    jmp short .x
.dis:
    call dc_dismiss
    jmp short .x
.enter:
    call dc_edit
    jmp short .x
.del:
    call dc_askdel
    jmp short .x
.esc:
    call dc_release
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

; dc_release - Esc: the bar and the keys back to the front application
; (OSAPI_WM_DESK RELEASE, SPEC.md 11.103.4) - and on kern_small, where there
; is none, nothing selected, as v1
dc_release:
    push ax
    push bx
    test byte [dia_wf], DIA_W_DESK
    jz .sel
    mov bx, [dc_win]
    mov al, 3
    call OSAPI_WM_DESK
    jmp short .x
.sel:
    xor bx, bx
    call dc_selset
.x:
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
; dc_oncard - DIA_ONCARD (AL = the card, CX, DX the press card-relative, SI =
; the window; W_ONCLICK's environment). A day, a task's box and a line act
; NOW; a button LIGHTS and waits for its release (dc_oncardup)
; -----------------------------------------------------------------------------
dc_oncard:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [dc_clip], 0
    call dc_abdismiss
    jc .x
    call dc_hit                 ; [dc_hk] / [dc_hi] / [dc_hb] and its rect
    mov bl, [dc_hk]
    cmp byte [dc_ed], 0
    je .shut
    cmp bl, DCH_FIELD           ; the field open: a press in it places the
    jne .x                      ; caret, and anything else waits
    call dc_fclick
    jmp short .x
.shut:
    cmp bl, DCH_DAY
    jne .b
    mov al, [dc_hi]
    call dc_select
    jmp short .x
.b:
    cmp bl, DCH_BTN
    jne .a
    inc al                      ; THE BUTTON: lit now, acted on at its release
    mov [dc_lit], al
    dec al
    mov ah, [dc_hb]
    mov [dc_lact], ah
    mov bx, [dc_hx0]
    mov [dc_lx0], bx            ; (and dc_lx1)
    mov bx, [dc_hy0]
    mov [dc_ly0], bx
    mov dx, [dc_hy1]
    mov [dc_ly1], dx
    call dc_arm
    jc .x
    mov cx, [dc_lx0]
    call dc_rrect
    jmp short .x
.a:
    mov bh, [dc_hi]
    cmp bl, DCH_ALINE
    jne .tb
    mov bl, DCS_APPT
    jmp short .sel
.tb:
    cmp bl, DCH_TBOX
    jne .tt
    mov al, bh
    call dc_toggle
    jmp short .x
.tt:
    cmp bl, DCH_TTEXT
    jne .x
    mov bl, DCS_TASK
.sel:
    cmp bl, [dc_selk]           ; the item ALREADY selected: edit it
    jne .ss
    cmp bh, [dc_seli]
    jne .ss
    call dc_edit
    jmp short .x
.ss:
    call dc_selset
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dc_fclick - a press in the open field (card-relative [dc_qx]/[dc_qy]):
; the caret to it - the old bar's cell repainted and the new bar drawn
dc_fclick:
    call dc_arm
    jc .r
    call dc_frect
    mov cx, [dia_fx]
    add cx, [dc_qx]
    mov al, 1
    call dia_ctop
    mov dx, [dia_fy]
    add dx, bx
    add dx, [dc_qy]
    mov si, dc_ln
    mov bx, [si + LN_CAR]       ; the bar's old cell
    call os88line_click
    jc .r                       ; outside the field
    mov ax, [si + LN_CAR]
    cmp ax, bx
    je .r                       ; the same cell: nothing moved
    xchg ax, bx
    call os88line_caroff
    xchg ax, bx
    call os88line_caron
.r:
    ret

; -----------------------------------------------------------------------------
; dc_oncardup - DIA_ONCARDUP (AL = the card the PRESS went to, CX, DX the
; release card-relative - it may be off the card): a held button un-lights,
; and acts when the release is inside it (SPEC.md 13.5). The un-light is
; owed (dc_lpend) and settled by the act's own redraw when that covers it
; -----------------------------------------------------------------------------
dc_oncardup:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    mov byte [dc_clip], 0
    mov bl, [dc_lit]
    or bl, bl
    jz .x
    mov [dc_lpend], bl
    mov byte [dc_lit], 0
    dec bl
    cmp al, bl
    jne .owe
    cmp dx, [dc_ly0]            ; inside its rect?
    jl .owe
    cmp dx, [dc_ly1]
    jg .owe
    or cx, cx
    js .owe
    shr cx, 1
    shr cx, 1
    shr cx, 1
    cmp cl, [dc_lx0]
    jb .owe
    cmp cl, [dc_lx1]
    ja .owe
    mov bl, [dc_lact]
    dec bl
    xor bh, bh
    shl bx, 1
    call [dc_bacts + bx]
.owe:
    mov bl, [dc_lpend]          ; still owed: drawn plain again
    or bl, bl
    jz .x
    mov byte [dc_lpend], 0
    call dc_arm
    jc .x
    mov al, bl
    dec al
    mov bx, [dc_ly0]
    mov dx, [dc_ly1]
    mov cx, [dc_lx0]
    call dc_rrect
.x:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dc_bacts:   dw dc_bprev, dc_bnext, dc_gotoday, dc_plus, dc_apager
            dw dc_tpager, dc_snooze, dc_dismiss, dc_pswap

dc_bprev:
    mov al, 0FFh
    jmp dc_month
dc_bnext:
    mov al, 1
    jmp dc_month

; dc_onfocus - DIA_ONFOCUS (AL = 1 gained the bar, 0 lost it): losing it
; closes an open field, as Esc does (SPEC.md 110.3)
dc_onfocus:
    mov byte [dc_clip], 0
    or al, al
    jnz .r
    cmp byte [dc_ed], 0
    je .r
    call dc_fclose
.r:
    ret

; -----------------------------------------------------------------------------
; dc_oncmd - AM_ONCMD (AL = the item, AH = the menu; lock held, no clip). A
; pick closes the entry field as Esc does, first (SPEC.md 110.3)
; -----------------------------------------------------------------------------
dc_oncmd:
    push ax
    push bx
    mov byte [dc_clip], 0
    call dc_abdismiss           ; a pick takes the card down - one repaint -
                                ; then acts on what it shows
    cmp byte [dc_ed], 0
    je .shut
    call dc_fclose
.shut:
    cmp ah, 1
    je .remind
    cmp al, DCM_TODAY
    jae .month
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [dc_cmds + bx]
    jmp short .x
.remind:
    call dc_remind
    jmp short .x
.month:
    je .today
    cmp al, DCM_PREV
    je .prev
    mov al, 1
    call dc_month
    jmp short .x
.prev:
    mov al, 0FFh
    call dc_month
    jmp short .x
.today:
    call dc_gotoday
.x:
    pop bx
    pop ax
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
    je .ne                      ; it, throwing its line away
    mov word [dc_i_cal + 2 * DCM_NEWA], dc_i_newa1
    mov word [dc_i_cal + 2 * DCM_NEWT], dc_i_newt1
    mov word [dc_i_cal + 2 * DCM_EDIT], dc_i_edit1
.ne:
    mov ax, dc_i_clr
    call dc_anydone
    jnc .c
    mov ax, dc_i_clr0
.c:
    mov [dc_i_cal + 2 * DCM_CLEAR], ax
    mov ax, dc_i_pane0          ; Show Tasks / Reminder / Day: COMPACT's slot
    cmp byte [dia_li], 2
    jne .p
    mov ax, dc_i_showt
    cmp byte [dc_pane], 1
    jb .p
    mov ax, dc_i_showr
    je .p
    mov ax, dc_i_showd
.p:
    mov [dc_i_cal + 2 * DCM_PANE], ax
    pop di
    pop ax
    popf
    ret

; dc_selset - select BL = 0 (nothing), DCS_APPT or DCS_TASK, index BH: on
; TALL / MID the old line's rows and the new one's - ONE dc_rrows when their
; span fits a band, two when not - and on COMPACT's slot card 2 whole. Every
; register kept
dc_selset:
    push ax
    mov al, [dc_selk]
    mov ah, [dc_seli]
    mov [dc_selk], bl
    mov [dc_seli], bh
    call dc_menulive
    cmp ax, bx
    je .x
    call dc_arm
    jc .x
    cmp byte [dia_li], 2
    jne .lines
    call dc_rday
    jmp short .x
.lines:
    push bx
    push cx
    push dx
    push si
    push di
    mov si, bx                  ; SI = the new selection
    call dc_selrows             ; BX..DX = the old line's rows
    jnc .old
    mov ax, si
    call dc_selrows             ; the new line alone
    jc .done
    jmp short .one
.old:
    mov cx, bx
    mov di, dx                  ; CX..DI = the old line's
    mov ax, si
    call dc_selrows             ; BX..DX = the new line's
    jc .oldonly
    push bx
    push dx
    cmp cx, bx
    jge .l1
    mov bx, cx                  ; the span: the lower first...
.l1:
    cmp di, dx
    jle .l2
    mov dx, di                  ; ...the higher last
.l2:
    mov ax, dx
    sub ax, bx
    cmp ax, GFXE_BAND_H
    jae .two
    add sp, 4                   ; one band holds both: one BLIT1
    jmp short .one
.two:
    pop dx
    pop bx
    mov al, 1
    call dc_rrows               ; the new line...
.oldonly:
    mov bx, cx                  ; ...and the old
    mov dx, di
.one:
    mov al, 1
    call dc_rrows
.done:
    pop di
    pop si
    pop dx
    pop cx
    pop bx
.x:
    pop ax
    ret

; dc_selrows - the card-2 rows selection AL (DCS_*) index AH is drawn on
; (TALL / MID): BX..DX, its line's whole pitch; CF = 1 it is not on show
; (nothing, another page, the pager's line). AX kept
dc_selrows:
    push ax
    push cx
    push di
    mov di, [dc_geo]
    cmp al, DCS_APPT
    je .ap
    cmp al, DCS_TASK
    jne .no
    mov al, ah
    sub al, [dc_ttop]
    jb .no
    mov cl, [di + DCG_TN]
    mov ch, [DIA_PAY + DCP_NTASK]
    call .fit
    jc .no
    call dc_trow                ; DX = the box's top
    mov bx, dx
    mov al, [di + DCG_TP]
    jmp short .span
.ap:
    mov al, ah
    sub al, [dc_afirst]
    jb .no
    sub al, [dc_atop]
    jb .no
    mov cl, [di + DCG_AN]
    mov ch, [dc_acnt]
    call .fit
    jc .no
    call dc_arow                ; DX = the line's text row
    mov bl, [di + DCG_BAR]
    xor bh, bh
    sub dx, bx
    mov bx, dx                  ; its bar's first row
    mov al, [di + DCG_AP]
.span:
    xor ah, ah
    add dx, ax
    dec dx
    clc
    jmp short .x
.no:
    stc
.x:
    pop di
    pop cx
    pop ax
    ret
.fit:                           ; AL = the line, CL = lines, CH = items:
    cmp ch, cl                  ; CF = 0 it is an item's line
    jbe .f1
    dec cl                      ; paging: the last line is the pager
.f1:
    cmp al, cl
    jae .fno
    clc
    ret
.fno:
    stc
    ret

; dc_pswap - COMPACT's slot: the next page (Tab, a press on its heading, the
; menu). The user's paging is the user's: a ringing reminder's hold on the
; slot is given up. Nothing on TALL / MID, which show all three cards
dc_pswap:
    cmp byte [dia_li], 2
    jne .r
    push ax
    mov al, [dc_pane]
    inc ax
    cmp al, 3
    jb .s
    xor al, al
.s:
    mov [dc_pane], al
    mov byte [dc_pane0], 0FFh
    call dc_menulive
    call dc_arm
    jc .x
    call dc_rday
.x:
    pop ax
.r:
    ret

; dc_apager / dc_tpager - `+N more`: the list's next page, the last page's
; the first
dc_apager:
    push ax
    push bx
    mov bx, [dc_geo]
    mov al, [bx + DCG_AN]
    dec ax
    add al, [dc_atop]
    cmp al, [dc_acnt]
    jb .s
    xor al, al
.s:
    mov [dc_atop], al
    jmp short dc_pgdraw

dc_tpager:
    push ax
    push bx
    mov bx, [dc_geo]
    mov al, [bx + DCG_TN]
    dec ax
    add al, [dc_ttop]
    cmp al, [DIA_PAY + DCP_NTASK]
    jb .s
    xor al, al
.s:
    mov [dc_ttop], al
dc_pgdraw:
    call dc_arm
    jc .x
    call dc_rday
.x:
    pop bx
    pop ax
    ret

; dc_plus - `+`: the field for EITHER (the parse decides) - or, on COMPACT's
; tasks page, for a task
dc_plus:
    cmp byte [dia_li], 2
    jne .any
    cmp byte [dc_pane], 1
    je dc_newt
.any:
    call dc_busy
    jc .r
    push ax
    mov al, DCE_NEW
    mov byte [dc_ebuf], 0
    call dc_fopen
    pop ax
.r:
    ret

; dc_newa / dc_newt - the field, empty, for a new appointment on the selected
; day / a new task; refused with the reason at a full table (SPEC.md 110.3)
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

; dc_edit - the field holding the selected item (Enter, a second press, Edit
; Item)
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

; dc_fopen - open the field, mode AL (DCE_*), holding what dc_ebuf holds, in
; card 2's heading row - on COMPACT with the page its list is on
dc_fopen:
    push ax
    push cx
    mov [dc_ed], al
    call dc_menulive            ; the three that would close it grey
    mov word [dc_ln + LN_BUF], dc_ebuf
    mov word [dc_ln + LN_MAX], DC_ESZ
    cmp al, DCE_NEWT
    jb .m
    cmp al, DCE_EDT
    ja .m
    mov word [dc_ln + LN_MAX], DCT_TMAX + 1
.m:
    mov byte [dc_ln + LN_FOCUS], 1
    mov byte [dc_ln + LN_DIS], 0
    push si
    mov si, dc_ln
    call os88line_resync        ; the caret at the end of what is there
    pop si
    xor cl, cl                  ; CL = 1: COMPACT turned to another page
    cmp byte [dia_li], 2
    jne .a
    mov ah, 0                   ; the page its list is on: the day's, or the
    cmp al, DCE_NEWT            ; tasks'; `+` stays where it was pressed
    jb .pg
    cmp al, DCE_EDT
    ja .a
    inc ah
.pg:
    cmp [dc_pane], ah
    je .a
    mov [dc_pane], ah
    mov byte [dc_pane0], 0FFh
    call dc_menulive
    inc cx
.a:
    call dc_arm
    jc .x
    or cl, cl
    jz .d
    call dc_rday                ; the page, and the field over its heading
    jmp short .x                ; (dc_fover) - its heading not composed
.d:
    call dc_fdraw
.x:
    pop cx
    pop ax
    ret

; dc_fclose - shut the field (Esc, an empty Enter, a menu pick, a commit,
; the bar lost): the heading's rows recomposed, one BLIT1
dc_fclose:
    push ax
    push bx
    push dx
    mov byte [dc_ed], 0
    mov byte [dc_ln + LN_FOCUS], 0
    call dc_menulive            ; ...and are live again
    call dc_arm
    jc .x
    call dc_fhead               ; BX..DX = its rows
    mov al, 1
    call dc_rrows
    call dc_c3sync              ; COMPACT: a reminder that rang meanwhile
.x:
    pop dx
    pop bx
    pop ax
    ret

; dc_fenter - Enter in the field: keep what it says. `+`'s field is an
; appointment when the line starts with a time, else a task
dc_fenter:
    push ax
    mov ah, [dc_ed]             ; the mode the field was opened in
    cmp ah, DCE_NEW
    jne .k
    push bx
    push cx
    push si
    mov si, dc_ebuf
    push word [dc_ebuf]         ; the parse trims: look, then put it back
    call dc_ptime               ; BX = the time, DC_ALLDAY none
    pop word [dc_ebuf]
    mov byte [dc_ed], DCE_NEWA
    cmp bx, DC_ALLDAY
    jne .t
    mov byte [dc_ed], DCE_NEWT
.t:
    pop si
    pop cx
    pop bx
.k:
    cmp byte [dc_ed], DCE_NEWT
    jb .a
    call dc_ctask
    jmp short .z
.a:
    call dc_cappt
.z:
    cmp byte [dc_ed], 0         ; REFUSED (too long): the field stays open
    je .r                       ; in the mode it was opened in - `+`'s
    mov [dc_ed], ah             ; still for either, an edit still an edit
.r:
    pop ax
    ret

; dc_ptime - BX = the time the line SI starts with, DC_ALLDAY when it starts
; with none (dc_parse's first word, on a copy). Every other register kept
dc_ptime:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    push si                     ; the line to dc_rec, which is scratch until
    mov di, dc_rec              ; dc_cappt builds its record there
    mov cx, DC_ESZ
    cld
    rep movsb
    pop si
    mov si, dc_rec
    call dc_parse               ; BX = the time
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

; dc_cappt - the field's line as an appointment on the selected day (SPEC.md
; 110.3): parsed, refused when its text is too long, inserted in order -
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
    cmp byte [dc_ed], DCE_EDA
    je .room
    cmp byte [DIA_PAY + DCP_NAPPT], DCP_MAXAPPT
    jb .room
    mov si, dc_s_afull          ; `+`'s line at a full table
    call dc_toast
    jmp .shut
.room:
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
    cmp byte [dc_ed], DCE_EDA
    jne .ins
    mov al, [dc_edi]            ; EDITED: out first, and its reminder kept
    cmp al, [dc_c3a]            ; (card 3's own: its text may have changed
    jne .nc3                    ; where dc_c3calc's compare cannot see)
    mov byte [dc_c3f], 1
.nc3:
    call dc_aptr                ; while it stays timed (its FIRED, DUE and
    mov dx, [si + DCA_TIME]     ; SNOOZED only while its time does not move)
    cmp dx, DC_ALLDAY
    je .del
    cmp bx, DC_ALLDAY
    je .del
    mov ah, [si + DCA_FLAGS]
    cmp dx, bx
    jne .moved
    mov byte [dc_fresh], 0      ; the same time: its marks stand
    jmp short .same
.moved:
    and ah, ~(DCA_FIRED | DCA_DUE | DCA_SNOOZED) & 0FFh
.same:
    mov [dc_rec + DCA_FLAGS], ah
    mov ah, [si + DCA_LEAD]
    mov [dc_rec + DCA_LEAD], ah
.del:
    call dc_adel
    call dc_fixsel
.ins:
    call dc_ains                ; AL = where it went
    jc .shut
    cmp byte [dc_fresh], 0
    je .kept
    call dc_stale               ; a moment already past: fired, silently
.kept:
    mov byte [dc_selk], DCS_APPT
    mov [dc_seli], al
    call dc_touch
    mov byte [dc_ed], 0         ; shut, unseen: card 2 is drawn whole below
    mov byte [dc_ln + LN_FOCUS], 0
    call dc_arange
    call dc_aview               ; its page
    mov al, [dc_sel]
    xor dl, dl
    call dc_marked              ; ZF = 0: its dot was there already
    jz .nod
    inc dx
.nod:
    call dc_build               ; the day's dot
    call dc_menulive
    call dc_arm
    jc .x
    or dl, dl
    jnz .box                    ; the box unchanged: not drawn again
    call dc_sidx
    call dc_rbox
.box:
    call dc_cdraw
    jmp short .x
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
    or cx, cx
    jz .shut0
    cmp cx, DCT_TMAX
    jbe .fits
    mov si, dc_s_tlong          ; `+`'s line, typed in a 31-character field:
    call dc_toast               ; refused, never cut (v1's rule) - the field
    mov si, dc_ln               ; stays open (dc_fenter puts its mode back)
    call os88line_resync
    call dc_arm
    jc .x
    call dc_fdraw
    jmp short .x
.fits:
    mov al, [dc_edi]
    cmp byte [dc_ed], DCE_EDT
    je .set
    mov al, [DIA_PAY + DCP_NTASK]
    cmp al, DCP_MAXTASK
    jb .new
    push si
    mov si, dc_s_tfull
    call dc_toast
    pop si
    jmp short .shut
.new:
    push si
    call dc_tptr
    mov word [si + DCT_FLAGS], 0
    mov word [si + DCT_DUE], 0
    pop si
    inc byte [DIA_PAY + DCP_NTASK]
.set:
    call dc_tset
    mov byte [dc_selk], DCS_TASK
    mov [dc_seli], al
    call dc_touch
    mov byte [dc_ed], 0         ; shut, unseen: card 2 is drawn whole below
    mov byte [dc_ln + LN_FOCUS], 0
    call dc_tview               ; its page
    cmp byte [dia_li], 2
    jne .draw
    mov byte [dc_pane], 1       ; COMPACT: the tasks' page shows it
    mov byte [dc_pane0], 0FFh
.draw:
    call dc_menulive
    call dc_arm
    jc .x
    call dc_cdraw
    jmp short .x
.shut0:
.shut:
    call dc_fclose
.x:
    pop si
    pop cx
    pop bx
    pop ax
    ret

; dc_cdraw - a commit's redraw: card 3 when it changed (dc_c3sync), then
; card 2 - unless, on COMPACT, a reminder that rang while the field was open
; just TOOK the slot, which dc_c3sync drew. The clip armed. Every register
; kept
dc_cdraw:
    call dc_c3sync
    cmp byte [dia_li], 2
    jne .d
    cmp byte [dc_pane], 2
    je .r
.d:
    call dc_rday
.r:
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

; dc_apage / dc_tpage - BL = items a page, BH = 1 when there is more than
; one page (and the last line is the pager). Every other register kept
dc_apage:
    push di
    mov di, [dc_geo]
    mov bl, [di + DCG_AN]
    xor bh, bh
    cmp [dc_acnt], bl
    jbe .x
    dec bl
    inc bh
.x:
    pop di
    ret

dc_tpage:
    push di
    mov di, [dc_geo]
    mov bl, [di + DCG_TN]
    xor bh, bh
    cmp [DIA_PAY + DCP_NTASK], bl
    jbe .x
    dec bl
    inc bh
.x:
    pop di
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
; 110.3). Nothing changes while the question is up (dc_busy). The question
; is an ordinary window: it lands above every application and the widget
; and takes the bar (SPEC.md 11.103.4)
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
    call OSAPI_WM_CLIP_CLEAR    ; the create recomposes the bar under any
    mov byte [dc_clip], 0       ; armed clip, and the titles' cells would be
                                ; recorded drawn and dropped (SPEC.md 110.9)
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
    mov byte [dc_clip], 0
    mov byte [dc_ask], 0
    or al, al
    jnz .x
    mov al, [dc_deli]
    cmp byte [dc_delk], DCS_APPT
    jne .task
    call dc_adel
    call dc_fixsel
    call dc_touch
    call dc_arange
    mov al, [dc_atop]           ; a page that emptied: the first...
    cmp al, [dc_acnt]
    jb .ka
    xor al, al
.ka:
    add al, [dc_afirst]         ; ...and the page holding the top NOW
    call dc_aview
    call dc_build               ; the day's dot
    call dc_menulive
    call dc_arm
    jc .c3
    call dc_sidx
    call dc_rbox
    call dc_rday
.c3:
    mov byte [dc_c3f], 1
    call dc_c3sync
    jmp short .x
.task:
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
    mov al, [dc_ttop]           ; a page that emptied: the first...
    cmp al, [DIA_PAY + DCP_NTASK]
    jb .tk
    xor al, al
.tk:
    call dc_tview               ; ...and the page holding the top NOW
    call dc_menulive
    call dc_arm
    jc .x
    call dc_rday
.x:
    pop bx
    pop ax
    ret

; dc_toggle - task AL checked or not (Space, a press on its box): the box
; redrawn, ONE sub-rect BLIT1 and nothing else (SPEC.md 110.3)
dc_toggle:
    call dc_busy
    jc .r
    push si
    call dc_tptr
    xor byte [si + DCT_FLAGS], DCT_DONE
    call dc_touch
    call dc_menulive
    call dc_arm
    jc .x
    call dc_rtbox
.x:
    pop si
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
    call dc_rday
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
    and byte [si + DCA_FLAGS], ~(DCA_REMIND | DCA_FIRED | DCA_DUE | DCA_SNOOZED) & 0FFh
    cmp ah, 0FFh
    je .one
    or byte [si + DCA_FLAGS], DCA_REMIND
    mov [si + DCA_LEAD], ah
.one:
    call dc_stale               ; armed for a moment already past: fired
    call dc_c3sync
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
    mov di, dc_line
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
; new DATE recomposes today's page, today's old box and new, card 2's
; heading and card 3; nothing is drawn while the About card is up - the
; repaint that takes it down shows the new today (SPEC.md 110.3). Then the
; reminders' minute (dcrem.inc), and the second note of a chirp one beat
; after the first.
; out: CF = 1 HOLD the save owed (os88dia.inc): the entry field, our Delete
;      question is open, or an edit landed since the last poll - a save is
;      seconds of disk with the lock held (SPEC.md 110.6), so it waits for a
;      quiet poll and a burst of edits is one save
; -----------------------------------------------------------------------------
dc_tick:
    push ax
    push bx
    mov byte [dc_clip], 0
    mov ax, [dc_ltp]            ; the launch's summary (dc_missed), said HERE:
    or ax, ax                   ; the first registration writes SYSTEM.CFG
    jz .ch                      ; on this same beat, before us, and CTRL.DRV
    mov word [dc_ltp], 0        ; says `Settings Saved` for it - from the
    push si                     ; entry proc the line was gone in seconds
    mov si, ax                  ; (SPEC.md 110.3)
    call dc_toast
    pop si
    jmp short .hold             ; ...and nothing else this beat
.ch:
    cmp byte [dc_chirp], 0
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
    mov byte [dc_c3f], 1        ; ...and card 3's NEXT is another day's
    cmp byte [dc_abon], 0
    jne .min
    call dc_arm
    jc .min                     ; not visible: the next paint draws it
    call dc_rhdr                ; today's page
    mov al, bl
    call dc_rbox                ; the old today's box...
    call dc_tidx
    call dc_rbox                ; ...and the new one's
    call dc_rday                ; `Today`, or the selected day's name
.min:
    call dc_rpass               ; the reminders: nothing unless the minute moved
    cmp byte [dc_c3f], 0
    je .hold
    call dc_c3sync              ; a new day's card 3
.hold:
    mov al, [dc_tq]             ; an edit this poll...
    mov byte [dc_tq], 0
    or al, [dc_ed]              ; ...the field open, or the Delete
    or al, [dc_ask]             ; question up: CF = 1 for any
    neg al
    pop bx
    pop ax
    ret

; --- the window (SPEC.md 110.3, 110.11) --------------------------------------
dc_tpl:
    dw 375, 42, 178, 357        ; dia_load sizes and places it (DIA_RIGHT)
    dw dc_title, dc_paint, dc_onkey, 0

dc_title:   db 'Desk Calendar', 0

    OS88_MENUSET dc_menus, dc_title, dc_oncmd   ; AM_NAME NOT 0: menu_relayout's
                                ; instance-name fallback sets the cells'
                                ; segment to the KERNEL's, so a package that
                                ; leaves it 0 gets its menus read out of
                                ; kernel memory (SPEC.md 110.9)
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
dc_i_showr: db 'Show Reminder  Tab', 0
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
dc_s_tlong: db 'A task: at most 23', 0
dc_s_qappt: db 'Delete this appointment?', 0
dc_s_qtask: db 'Delete this task?', 0

dc_ablines: dw dc_ab1, dc_ab2, dc_ab3, dc_ab4, 0
%if ($ - dc_ablines) / 2 - 1 != DC_ABN
  %error "deskcal: DC_ABN is dc_ablines' count"
%endif
dc_ab1:     db 'Desk Calendar', 0   ; each line inside the 176-px face
dc_ab2:     db 'A desk widget', 0
dc_ab3:     db 0
dc_ab4:     db 'SPEC.md 110', 0

%include "dcdate.inc"
%include "dcdata.inc"
%include "dcrem.inc"

%define OS88UI_ABOUT
%define OS88UI_ALERT
%define OS88UI_NOGLYPH          ; no check-box glyph: the widget's tick is a
                                ; composed bit record (SPEC.md 13.16.2.1)
%include "os88ui.inc"
%include "os88line.inc"
%include "os88gfx.inc"
%include "os88dia.inc"
%include "dcdraw.inc"           ; AFTER os88dia.inc: DIA_LAYOUT is its macro

%if DC_FX1 + LN_INSET > LN_CELL
  %error "deskcal: the entry field's inset must leave its pen on face x 8 (dc_frect)"
%endif

%if OS88LINE_SZ != 20
  %error "deskcal: its bss holds a 20-byte field block"
%endif

    align 16, db 0              ; os88dia.inc's read lands on a paragraph
    OS88_BSS DIA_BSS_END
    OS88_IMAGE_END
