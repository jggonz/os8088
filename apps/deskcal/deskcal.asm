; =============================================================================
; os8088 - apps/deskcal/deskcal.asm
;
; THE DESK CALENDAR - the first STICKY APP (SPEC.md 110,
; its own section 110.3). A month on the desktop that stays where it was put
; and comes back at the next boot: it ships in STIKAPPS\ on the system disk
; and includes apps/os88stik.inc, which owns its place, its state file
; (SYSTEM\APPDATA\DESKCAL.DAT) and its row in the desktop configuration.
;
; v2 (wave 4) is a WIDGET: three white cards ON the desk - under every
; application window, chromeless, no dock tile - with the desktop's own
; ground between them and a name tag under them that turns into a handle
; while the pointer is over it (os88stik.inc's STIK_WIDGET, SPEC.md 110.11).
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

%ifndef APP_SMALL               ; kern_big's: the Sticky App code is the ONE
%define STIK_LIB                ; copy in DESK.DRV (SPEC.md 110.15). Before
%endif                          ; the header: OS88_STIKCARD_END lays its block
%include "os88api.inc"

    OS88_HEADER 'Desk Calendar', dc_entry, OS88_F_ICON | OS88_F_DESK | OS88_F_STIK, OS88_HIGH | OS88_HIGHMEM

; A calendar page: a solid header band over a grid of days. It is also the
; Sticky App row's picture (STIK_ICON below), so a row the boot could not launch
; turns into a desktop icon that looks like what it was (SPEC.md 110.4).
    OS88_ICON16
    dw 0x0000, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x0000, 0x0000
    dw 0x0000, 0x7FFE, 0x6DB6, 0x7FFE, 0x4002, 0x5552, 0x4002, 0x5552
    dw 0x4002, 0x5552, 0x4002, 0x5552, 0x4002, 0x7FFE, 0x0000, 0x0000
    OS88_ICON16_END

; The Sticky Apps menu's row for us (SPEC.md 110.12): what Locator's menu
; lists before the calendar has ever been launched - today's page, big, over
; the month's grid - read out of this file's first sector.
    OS88_STIKCARD 'Desk Calendar', 'Dates, tasks & reminders'
    OS88_STIKROW '................................'
    OS88_STIKROW '.##############################.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#....######..........##......#.'
    OS88_STIKROW '.#..##......##......####......#.'
    OS88_STIKROW '.#..........##....##..##......#.'
    OS88_STIKROW '.#........##....##....##......#.'
    OS88_STIKROW '.#......##......##########....#.'
    OS88_STIKROW '.#....##..............##......#.'
    OS88_STIKROW '.#..##########........##......#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#.##########################.#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#...##..##..##..##..##..##...#.'
    OS88_STIKROW '.#...##..##..##..##..##..##...#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#...##..##..##..##..##..##...#.'
    OS88_STIKROW '.#...##..##..##..##..##..##...#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#...##..##..##..##..##..##...#.'
    OS88_STIKROW '.#...##..##..##..##..##..##...#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.#...##..##...................#.'
    OS88_STIKROW '.#...##..##...................#.'
    OS88_STIKROW '.#............................#.'
    OS88_STIKROW '.##############################.'
    OS88_STIKROW '................................'
    OS88_STIKCARD_END

DC_GRIDN    equ 42

; --- the payload v1, what the calendar reads of it (SPEC.md 110.3) -----------
; tools/os88stik.py's DESKCAL_* are the same numbers; t_stik holds them equal.
; The file is PACKED to the records in use (dcdata.inc), so the table's size
; costs a disk nothing until it is filled (SPEC.md 110.3)
%define DC_NAPPT 48             ; (the toasts below spell these, so the counts
%define DC_NTASK 32             ; are said once)
DCP_LEN     equ 2440
DCP_VER     equ 1
DCP_NAPPT   equ 0               ; db: appointments in use
DCP_NTASK   equ 1               ; db: tasks in use
DCP_SNZT    equ 4               ; dw: the SNOOZE SLOT's minute of the day (v2)
DCP_SNZD    equ 6               ; dw: ...and its FAT date - read only beside a
                                ; SNOOZED appointment, so v1's zeros mean nothing
DCP_MAXAPPT equ DC_NAPPT
DCP_MAXTASK equ DC_NTASK
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

; --- the Sticky App (apps/os88stik.inc, SPEC.md 110.5, 110.11) -----------------------
%define STIK_DATNAME 'DESKCAL.DAT'
%define STIK_CAP     'Calendar'
%define STIK_ISOPEN  'Desk Calendar is open'
%define STIK_DMGREPLACE         ; a damaged file is replaced (110.5.2)
%define STIK_PACK     dc_squeeze ; the state file holds the records in use
%define STIK_UNPACK   dc_spread  ; (dcdata.inc, SPEC.md 110.5.3)
%define STIK_ONTICK  dc_tick
STIK_PAYLEN equ DCP_LEN
STIK_PAYVER equ DCP_VER
STIK_ICON   equ 32
STIK_BSS    equ DC_BSS          ; the include's state follows ours
%define STIK_WIDGET
%ifndef APP_SMALL               ; kern_big's build: a desk widget or nothing.
%define STIK_DESKONLY           ; The small build (make small) keeps the
%endif                          ; titled fallback and the worker arm (110.7)
%define STIK_TAG      'Desk Calendar'   ; = the title
%define STIK_RIGHT    264       ; the face opens 264 px in from the right edge
                                ; (a %define: the include tests it with %ifdef)
%define STIK_LAYOUTS  dc_lays
%define STIK_PAINTCARD dc_pcard
%define STIK_COMPOSE  dc_compose
%define STIK_ONCARD   dc_oncard
%define STIK_ONCARDUP dc_oncardup
%define STIK_ONFOCUS  dc_onfocus
%define GFXE_TEXT
%define GFXE_BITS
%define GFXE_BAND_W   176       ; the face: 22 cells, a 704-byte band
%define GFXE_BAND_H   32
%define GFXE_BAND_BUF dc_band
%define GFXE_SCALE              ; the day's x3 and the headings' x2 (5.12.8).
                                ; No GFXE_BAND: the face papers and blits its
                                ; own band, so no bounds, pixel or gfxe_bput
                                ; (SPEC.md 5.12.9)

; --- bss: OURS FIRST, at fixed offsets - tests read it out of a running guest
; (tests/deskcal*.py), and tools/os88stik.py's DESKCAL_BSS_* say where
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
dc_ask      equ os88_image_end + 70     ; db Delete's latch: 2 armed by this
                                        ; event, 1 by the one before (dc_askdel)
dc_delk     equ os88_image_end + 71     ; db ...for this kind
dc_deli     equ os88_image_end + 72     ; db ...and index - one word
dc_afirst   equ os88_image_end + 73     ; db the day's first appointment
DC_BSS_ACNT equ 74
dc_acnt     equ os88_image_end + DC_BSS_ACNT    ; db ...and how many
dc_ph       equ os88_image_end + 75     ; db spare (was the parser's hour)
dc_tq       equ os88_image_end + 76     ; db an edit since the last poll
dc_chirp    equ os88_image_end + 77     ; db the chirp's second note is owed
dc_rtc      equ os88_image_end + 78     ; db OSAPI_CLK_GET's AL: bit 0 an RTC
dc_fresh    equ os88_image_end + 79     ; db spare (was dc_cappt's "armed afresh")
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
; THE REGISTERS. Every handler below is a window callback, or a hook
; os88stik.inc calls with the registers it needs banked: W_ONKEY and AM_ONCMD
; may clobber AX-DI and ES (SPEC.md 12.2, 13); STIK_ONCARD / STIK_ONCARDUP
; are inside stik_click / stik_mup, which bank everything and give SI back
; themselves; STIK_ONTICK is inside stik_beat, which banks AX-DI and ES; and
; STIK_ONFOCUS is inside stik_focus, which banks AX, BX and SI - so
; dc_onfocus keeps CX, DX, DI. So the actions they reach - nearly everything
; in this file - keep NOTHING unless their header says so, and most are
; JUMPED to, returning for the handler. Of this file's routines the other
; files call only the hooks (os88stik.inc) and dc_touch, dc_busy, dc_toast,
; dc_menulive and dc_copyz (dcrem.inc), whose contracts are kept to the
; letter.
; -----------------------------------------------------------------------------

; dc_compact - ZF = 1 the layout is COMPACT (a CGA's: cards 2 and 3 paged
; through one slot). Keeps every register
dc_compact:
    cmp byte [stik_li], 2
.r:
    ret

; -----------------------------------------------------------------------------
; dc_entry - the entry proc (DS = CS = ours, ES = KERNEL_SEG, the UI task)
; in:  SI = the loader's name, for os88stik.inc
; out: CF = 0 launched, BX = the window; CF = 1 not (another calendar is up,
;      or no window). The rest clobbered: the loader reloads what it reads
;      (SPEC.md 20.2)
; -----------------------------------------------------------------------------
dc_entry:
    mov di, dc_tpl
    call stik_load              ; QUERY, the layout by the screen's height,
    jc dc_compact.r             ; the template's size and place (110.11)
    mov al, [stik_li]
    mov ah, DCG_SZ
    mul ah
    add ax, dc_geos
    mov [dc_geo], ax            ; our own geometry for that layout
    call dc_check               ; a payload whose counts or order are not
    jnc .ok                     ; ours is a DAMAGED file: empty, and the
    push es                     ; first save replaces it (STIK_DMGREPLACE)
    push ds                     ; (SPEC.md 110.3, 110.5.2)
    pop es
    mov di, STIK_PAY
    mov cx, DCP_LEN
    xor al, al
    cld
    rep stosb
    pop es
    or byte [stik_st], STIK_S_DAMAGED
.ok:
    call dc_today               ; today, and the month on show is today's
    or word [dc_lnow], -1       ; no minute seen: the first beat looks
    call dc_missed              ; what came due while we were not running
    test byte [stik_st], STIK_S_DAMAGED
    jz .nd
    mov word [dc_ltp], dc_s_dmg ; ...or that the file was not ours: it runs
.nd:                            ; empty and the first save replaces it
    call dc_totoday
    call dc_arange              ; the day's appointments
    mov byte [dc_pane0], 0FFh
    call dc_c3calc              ; card 3 as the file left it - and a CGA's
    call dc_compact             ; slot opens on the reminder when it rings
    jne .mk
    cmp byte [dc_c3k], DC3_RING
    jne .mk
    mov word [dc_pane], 2       ; [dc_pane] 2, [dc_pane0] 0
.mk:
    mov si, dc_tpl
    call OSAPI_WM_CREATE        ; BX = the window, CF = the table is full
    jc .out
    mov [dc_win], bx
    OS88_REGION_MOVABLE_NOKEEP dc_entry.out ; AX, DX spent: the proc is a ret
    mov al, 1
    call OSAPI_WM_KEEPH         ; a fixed layout: hang over the dock, never cut
    call dc_compact             ; TALL / MID show both lists: no page
    je .m                       ; to choose, so no Show item (the layout
    dec word [dc_menus_list + AMENU_NITEM]  ; is the session's)
.m:
    mov si, dc_menus
    call OSAPI_MENU_SET
    mov si, dc_about
    call OSAPI_ABOUT_SET
    call dc_menulive            ; the greyed items, as the payload stands
    call stik_made              ; LAST: MAKE, the prefilters, the tick
    clc
.out:
    ret

; dc_totoday - the month on show := today's, today selected, the grid built.
; Clobbers AX
dc_totoday:
    mov ax, [dc_ty]
    mov [dc_y], ax
    mov ax, [dc_tm]             ; [dc_tm] [dc_td] -> [dc_m] [dc_sel]
    mov [dc_m], ax
    jmp dc_build

; -----------------------------------------------------------------------------
; dc_paint - W_PAINT (SI = the window; lock held, the clip armed): the
; include's paint - the ground, every card's slabs, the tag
; -----------------------------------------------------------------------------
dc_paint:
    mov byte [dc_clip], 1
    jmp stik_paint

; dc_about - the About item (SPEC.md 12.2, 20.5.1): one toast, and the face
; is never covered (SPEC.md 110.3). W_ONCLICK's terms: SI given back
dc_about:
    push si
    mov si, dc_s_about
    call dc_toast
    pop si
    ret

; dc_age - an EVENT (a key, a card's press, a menu pick): Delete's latch one
; event older (2 -> 1 -> 0, dc_askdel) - then dc_unclip. dc_unclip - every
; entry point's first act: the clip is not armed in this lock hold (dc_arm).
; Both keep every register
dc_age:
    shr byte [dc_ask], 1
dc_unclip:
    mov byte [dc_clip], 0
    ret

; -----------------------------------------------------------------------------
; dc_onkey - W_ONKEY (AL = ASCII, AH = scancode, SI = the window; lock held).
; With the entry field open every key is the field's (os88line_key) and only
; Enter and Esc come back (SPEC.md 110.3). Else the key is looked up in
; dc_keys - by its scancode, then its character, then the character either
; case - and the routine it names is JUMPED to, returning for us
; -----------------------------------------------------------------------------
dc_onkey:
    call dc_age
    call dc_busy
    jc dc_fkey
    cmp ah, KSC_DEL             ; Del - and not the keypad's `.`, which is
    jne .k                      ; the same scancode with an ASCII
    or al, al
    jz .del
    cmp al, 0E0h
    jne .k
.del:
    jmp dc_askdel
.k:
    mov di, sp                  ; the frame a hit returns from
    mov dx, ax
    xchg dl, dh                 ; DL = the scancode, DH = the character
    mov si, dc_keys
    call .find                  ; by scancode...
    mov dl, dh
    call .find                  ; ...by character...
    or dl, 0x20                 ; ...and letters either case: entered, so a
.find:                          ; miss here returns for dc_onkey
    lodsb
    or al, al
    jz .r                       ; the list's end
    cmp al, dl
    lodsw                       ; (no flags)
    jne .find
    mov sp, di
    jmp ax
.r:
    ret

; dc_fkey - dc_onkey with the field open: the key is the field's, or Enter /
; Esc
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
    jc .r                       ; not on the glass: the state is enough
    call dc_frect
    jmp os88line_edit           ; the cell it changed, and the caret
.key:
    cmp al, 13
    je dc_fenter
    cmp al, 27
    je dc_fclose
.r:
    ret

; dc_fenter - Enter in the field: keep what it says. `+`'s field is an
; appointment when the line starts with a time, else a task
dc_fenter:
    mov al, [dc_ed]
    mov ah, al
    push ax                     ; AH = the mode the field was opened in
    cmp al, DCE_NEW
    jne .k
    mov si, dc_ebuf             ; the parse trims, so it looks at a copy in
    mov di, dc_rec              ; dc_rec - scratch until dc_cappt builds its
    mov cx, DC_ESZ - 1          ; record there
    call dc_copyz
    mov si, dc_rec
    call dc_parse               ; BX = the time, DC_ALLDAY none
    mov al, DCE_NEWA
    inc bx                      ; DC_ALLDAY (0FFFFh): no time - a task
    jnz .t
    mov al, DCE_NEWT
.t:
    mov [dc_ed], al
.k:
    cmp al, DCE_NEWT
    jb .a
    call dc_ctask
    jmp .z
.a:
    call dc_cappt
.z:
    pop ax
    call dc_busy                ; REFUSED (too long): the field stays open
    jnc .r                      ; in the mode it was opened in - `+`'s
    mov [dc_ed], ah             ; still for either, an edit still an edit
.r:
    ret

; dc_fclose - shut the field (Esc, an empty Enter, a menu pick, a commit,
; the bar lost): the heading's rows recomposed, one BLIT1. Clobbers AX, BX,
; DX; keeps the rest (dc_onfocus's CX and DI)
dc_fclose:
    call dc_fshut
    call dc_menulive            ; ...and are live again
    call dc_arm
    jc .r
    call dc_fhead               ; BX..DX = its rows
    mov al, 1
    call dc_rrows
    jmp dc_c3sync               ; COMPACT: a reminder that rang meanwhile
.r:
    ret

; dc_fkeep - a commit: item AL of kind AH (DCS_*) selected, a save owed
; (dc_touch) and the field shut, unseen - card 2 is drawn whole after.
; dc_fshut - the field shut, unseen: [dc_ed] and its focus off. Both keep
; every register
dc_fkeep:
    xchg al, ah
    mov [dc_selk], ax
    xchg al, ah
    call dc_touch
dc_fshut:
    mov byte [dc_ed], 0
    mov byte [dc_ln + LN_FOCUS], 0
    ret

; dc_cappt - the field's line as an appointment on the selected day (SPEC.md
; 110.3): parsed, refused when its text is too long, inserted in order -
; an edited one taken out first, keeping its reminder while it stays timed.
; Clobbers everything, ES included
dc_cappt:
    mov si, dc_ebuf
    call dc_parse               ; BX = the time, SI = the text, CX = its length
    cmp bx, DC_ALLDAY
    jne .some
    jcxz .shut                  ; nothing typed: as Esc
.some:
    cmp cx, DCA_TMAX
    jbe .fits
    mov si, dc_s_long           ; the field stays open: never cut it
    jmp dc_frefuse
.fits:
    cmp byte [dc_ed], DCE_EDA
    je .room
    cmp byte [STIK_PAY + DCP_NAPPT], DCP_MAXAPPT
    jb .room
    mov si, dc_s_afull          ; `+`'s line at a full table
    call dc_toast
.shut:
    jmp dc_fclose
.room:
    push ds                     ; dc_rec := the new appointment: its text,
    pop es                      ; the record past it zeroed...
    cld
    mov di, dc_rec + DCA_TEXT
    rep movsb
    mov cx, dc_rec + DCP_APPTSZ
    sub cx, di
    xor al, al
    rep stosb                   ; (CX = 0: armed afresh, unless an edit keeps
    call dc_pack                ; its time)
    mov [dc_rec + DCA_DATE], ax ; ...and its first six bytes
    mov [dc_rec + DCA_TIME], bx
    mov ah, [STIK_PAY + DCP_LEAD]
    mov al, 0
    cmp bx, DC_ALLDAY
    je .fl
    test byte [STIK_PAY + DCP_FLAGS], DCF_NOREM
    jnz .fl
    mov al, DCA_REMIND
.fl:
    mov [dc_rec + DCA_FLAGS], ax    ; and DCA_LEAD
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
    cmp dx, bx
    mov dx, [si + DCA_FLAGS]    ; DL = its flags, DH = its lead
    jne .moved
    inc cx                      ; the same time: its marks stand
    jmp .same
.moved:
    and dl, ~(DCA_FIRED | DCA_DUE | DCA_SNOOZED) & 0FFh
.same:
    mov [dc_rec + DCA_FLAGS], dx
.del:
    mov ah, DCS_APPT
    call dc_adel
    call dc_fixsel
.ins:
    call dc_ains                ; AL = where it went
    jc dc_ctask.shut
    jcxz .st
    jmp .kept
.st:
    call dc_stale               ; a moment already past: fired, silently
.kept:
    mov ah, DCS_APPT
    call dc_fkeep               ; shut, unseen: card 2 is drawn whole below
    call dc_arange
    call dc_aview               ; its page
    mov al, [dc_sel]
    call dc_marked              ; ZF = 0: its dot was there already...
    lahf                        ; ...kept in AH
    call dc_build               ; the day's dot
    call dc_menulive
    call dc_arm
    jc .x
    sahf
    jnz .box                    ; the box unchanged: not drawn again
    call dc_sidx
    call dc_rbox
.box:
    jmp dc_cdraw
.x:
    ret

; dc_ctask - the field's line as a task: new at the list's end, or the
; edited one's text. Clobbers everything
dc_ctask:
    mov si, dc_ebuf
    call dc_trim
    jcxz .shut
    cmp cx, DCT_TMAX
    jbe .fits
    mov si, dc_s_tlong          ; `+`'s line, typed in a 31-character field:
    jmp dc_frefuse              ; refused, never cut (v1's rule) - the field
                                ; stays open (dc_fenter puts its mode back)
.fits:
    mov al, [dc_edi]
    cmp byte [dc_ed], DCE_EDT
    je .set
    mov al, [STIK_PAY + DCP_NTASK]
    cmp al, DCP_MAXTASK
    jb .new
    mov si, dc_s_tfull
    call dc_toast
.shut:
    jmp dc_fclose
.new:
    push si
    call dc_tptr
    xor dx, dx
    mov [si + DCT_FLAGS], dx    ; (and its pad byte)
    mov [si + DCT_DUE], dx
    pop si
    inc byte [STIK_PAY + DCP_NTASK]
.set:
    call dc_tset
    mov ah, DCS_TASK
    call dc_fkeep               ; shut, unseen: card 2 is drawn whole below
    call dc_tview               ; its page
    call dc_compact
    jne .draw
    mov word [dc_pane], 0FF01h  ; COMPACT: the tasks' page shows it, and
.draw:                          ; [dc_pane0] 0FFh
    call dc_menulive
    jmp dc_armcd

; dc_frefuse - the field's line REFUSED (too long): the toast SI says why and
; the field stays open - but the parse's trim may have NULed trailing spaces,
; so the field learns its length again and is drawn whole. dc_armfd - the
; field whole, when the window is on the glass. Both clobber AX-DI
dc_frefuse:
    call dc_toast
    mov si, dc_ln
    call os88line_resync
dc_armfd:
    call dc_arm
    jc .r
    jmp dc_fdraw
.r:
    ret

; dc_tview / dc_aview - [dc_ttop] := the page holding task AL / [dc_atop]
; := appointment AL's; dc_aview0 - the same, AL counted from the day's
; first already. Clobber AX, BX, DI
dc_tview:
    mov bx, [dc_geo]
    mov bl, [bx + DCG_TN]
    mov di, dc_ttop
    cmp [STIK_PAY + DCP_NTASK], bl
    jmp dc_pgtop

dc_aview:
    sub al, [dc_afirst]
dc_aview0:                      ; (AL from the day's first already)
    mov bx, [dc_geo]
    mov bl, [bx + DCG_AN]
    mov di, dc_atop
    cmp [dc_acnt], bl

; dc_pgtop - [DI] := the first item of the page holding item AL, BL = the
; list's lines, the flags from comparing its items with BL: no more items
; than lines is one page (top 0); more, and the last line is the pager -
; BL - 1 items a page. Clobbers AX, BL
dc_pgtop:
    mov ah, 0
    jbe .z
    dec bl
    div bl
    mul bl
    jmp .s
.z:
    xor al, al
.s:
    mov [di], al
    ret

; dc_fixsel - item AL of kind AH (DCS_*) has gone: a selection of it is
; nothing now, and one after it in its table is one index down. Keeps
; everything
dc_fixsel:
    cmp [dc_selk], ah
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

; -----------------------------------------------------------------------------
; dc_oncard - STIK_ONCARD (AL = the card, CX, DX the press card-relative, SI =
; the window; W_ONCLICK's environment). A day, a task's box and a line act
; NOW; a button LIGHTS and waits for its release (dc_oncardup)
; -----------------------------------------------------------------------------
dc_oncard:
    call dc_age
    call dc_hit                 ; [dc_hk] / [dc_hi] / [dc_hb] and its rect
    xor bx, bx
    mov bl, [dc_hk]
    call dc_busy
    jnc .shut
    cmp bl, DCH_FIELD           ; the field open: a press in it places the
    jne dc_fixsel.x             ; caret, and anything else waits
    call dc_arm                 ; the caret to it ([dc_qx]/[dc_qy]): the
    jc dc_fixsel.x              ; old bar's cell repainted and the new bar
    call dc_frect               ; drawn
    mov cx, [stik_fx]
    add cx, [dc_qx]
    mov al, 1
    call stik_ctop
    mov dx, [stik_fy]
    add dx, bx
    add dx, [dc_qy]
    mov si, dc_ln
    mov bx, [si + LN_CAR]       ; the bar's old cell
    call os88line_click
    jc .x                       ; outside the field
    mov ax, [si + LN_CAR]
    cmp ax, bx
    je .x                       ; the same cell: nothing moved
    xchg ax, bx
    call os88line_caroff
    xchg ax, bx
    jmp os88line_caron
.shut:                          ; BX = the kind, counted down: DCH_DAY,
    dec bx                      ; BTN, ALINE, TBOX, TTEXT are 1..5
    jnz .b
    mov al, [dc_hi]
    jmp dc_select
.b:
    dec bx
    jnz .a
    inc ax                      ; THE BUTTON: lit now, acted on at its release
    mov [dc_lit], al
    dec ax
    push es                     ; its DCB_*, bytes and rows: dc_hb.. to
    push ds                     ; dc_lact.., the same seven-byte layout
    pop es
    cld
    mov si, dc_hb
    mov di, dc_lact
    mov cx, 7
    rep movsb
    pop es
    jmp dc_rlit
.a:
    dec bx
    jnz .tb
    mov bl, DCS_APPT            ; an agenda line
    jmp .sel
.tb:
    dec bx
    jnz .tt
    mov al, [dc_hi]             ; a task's box
    jmp dc_toggle
.tt:
    dec bx
    jnz .x
    mov bl, DCS_TASK            ; a task's text
.sel:
    mov bh, [dc_hi]
    cmp bx, [dc_selk]           ; the item ALREADY selected: edit it
    jne dc_selset
    jmp dc_edit
.x:
    ret

; dc_cdraw - a commit's redraw: card 3 when it changed (dc_c3sync), then
; card 2 - unless, on COMPACT, a reminder that rang while the field was open
; just TOOK the slot, which dc_c3sync drew. The clip armed. dc_armcd - the
; same when the window is on the glass (dc_arm). Both clobber AL
dc_armcd:
    call dc_arm
    jc dc_oncard.x
dc_cdraw:
    call dc_c3sync
    call dc_compact
    jne dc_rday
    cmp byte [dc_pane], 2
    jne dc_rday
    ret

; dc_mlday - dc_menulive, then dc_armday. dc_armday - dc_rday when the
; window is on the glass (dc_arm). Both clobber AL
dc_mlday:
    call dc_menulive
dc_armday:
    call dc_arm
    jnc dc_rday
    ret

; dc_rday - card 2 whole (the slot on COMPACT). Clobbers AL
dc_rday:
    mov al, 1
    jmp dc_rcardw

; dc_selset - select BL = 0 (nothing), DCS_APPT or DCS_TASK, index BH: on
; TALL / MID the old line's rows and the new one's - ONE dc_rrows when their
; span fits a band, two when not - and on COMPACT's slot card 2 whole.
; Clobbers AX-DI
dc_selset:
    mov ax, [dc_selk]           ; AL, AH = the old kind and index
    mov [dc_selk], bx
    call dc_menulive
    cmp ax, bx
    je .x
    call dc_arm
    jc .x
    call dc_compact
    je dc_rday
    mov si, bx                  ; SI = the new selection
    call dc_selrows             ; BX..DX = the old line's rows
    jnc .old
    mov ax, si
    call dc_selrows             ; the new line alone
    jc .x
    jmp .one
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
    jmp .one
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
    jmp dc_rrows
.x:
    ret

; dc_selrows - the card-2 rows selection AL (DCS_*) index AH is drawn on
; (TALL / MID): BX..DX, its line's whole pitch; CF = 1 it is not on show
; (nothing, another page, the pager's line). Clobbers AX
dc_selrows:
    mov bx, [dc_geo]
    cmp al, DCS_APPT
    je .ap
    cmp al, DCS_TASK
    jne .no
    mov al, ah
    sub al, [dc_ttop]
    jb .no
    mov ah, [bx + DCG_TN]       ; paging: the last line is the pager
    cmp [STIK_PAY + DCP_NTASK], ah
    jbe .t1
    dec ah
.t1:
    cmp al, ah
    jae .no
    call dc_trow                ; DX = the box's top
    mov al, [bx + DCG_TP]
    mov bx, dx
    jmp .span
.ap:
    mov al, ah
    sub al, [dc_afirst]
    jb .no
    sub al, [dc_atop]
    jb .no
    mov ah, [bx + DCG_AN]
    cmp [dc_acnt], ah
    jbe .a1
    dec ah
.a1:
    cmp al, ah
    jae .no
    call dc_arow                ; DX = the line's text row
    sub dl, [bx + DCG_BAR]
    sbb dh, 0                   ; its bar's first row
    mov al, [bx + DCG_AP]
    mov bx, dx
.span:
    cbw                         ; (a geometry pitch)
    add dx, ax
    dec dx
    clc
    ret
.no:
    stc
    ret

; -----------------------------------------------------------------------------
; dc_oncardup - STIK_ONCARDUP (AL = the card the PRESS went to, CX, DX the
; release card-relative - it may be off the card): a held button un-lights,
; and acts when the release is inside it (SPEC.md 13.5). The un-light is
; owed (dc_lpend) and settled by the act's own redraw when that covers it
; -----------------------------------------------------------------------------
dc_oncardup:
    call dc_unclip
    xor bx, bx
    xchg bl, [dc_lit]
    or bl, bl
    jz dc_rlit.x
    mov [dc_lpend], bl
    dec bx                      ; BX = the button's card
    cmp al, bl
    jne .owe
    cmp dx, [dc_ly0]            ; inside its rect?
    jl .owe
    cmp dx, [dc_ly1]
    jg .owe
    or cx, cx
    js .owe
    xchg ax, cx
    mov cl, 3
    shr ax, cl
    cmp al, [dc_lx0]
    jb .owe
    cmp al, [dc_lx1]
    ja .owe
    mov bl, [dc_lact]           ; BH = 0
    shl bx, 1
    call [dc_bacts - 2 + bx]
.owe:
    xor bx, bx                  ; still owed: drawn plain again
    xchg bl, [dc_lpend]
    or bl, bl
    jz dc_rlit.x
    xchg ax, bx
    dec ax

; dc_rlit - the held button's rect on card AL ([dc_lx0].. / [dc_ly0]..)
; recomposed - lit, or plain again - when the window is on the glass.
; Clobbers BX, CX, DX
dc_rlit:
    call dc_arm
    jc .x
    mov bx, [dc_ly0]
    mov dx, [dc_ly1]
    mov cx, [dc_lx0]
    jmp dc_rrect
.x:
    ret

dc_bacts:   dw dc_bprev, dc_bnext, dc_gotoday, dc_plus, dc_apager
            dw dc_tpager, dc_snooze, dc_dismiss, dc_pswap

; dc_onfocus - STIK_ONFOCUS (AL = 1 gained the bar, 0 lost it): losing it
; closes an open field, as Esc does (SPEC.md 110.3). stik_focus keeps every
; register and banks only AX, BX, SI: CX, DX, DI are kept here
dc_onfocus:
    call dc_unclip
    or al, al
    jnz .r
    call dc_busy
    jnc .r
    push dx
    call dc_fclose
    pop dx
.r:
    ret

; -----------------------------------------------------------------------------
; dc_oncmd - AM_ONCMD (AL = the item, AH = the menu; lock held, no clip). A
; pick closes the entry field as Esc does, first (SPEC.md 110.3) - so no
; command below finds the field open
; -----------------------------------------------------------------------------
dc_oncmd:
    call dc_age
    call dc_busy
    jnc .shut
    push ax
    call dc_fclose
    pop ax
.shut:
    cmp ah, 1
    je dc_remind
    cbw
    xchg ax, bx
    shl bx, 1
    jmp [dc_cmds + bx]

; dc_remind - Remind item AL: the selected TIMED appointment's lead (or its
; reminder off), else the default for new ones; the toast says which
dc_remind:
    cbw
    xchg ax, bx
    mov ah, [dc_leads + bx]     ; AH = the lead, or 0FFh: no reminder
    shl bx, 1
    mov bx, [dc_rlbl + bx]      ; BX = its words
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
    mov si, dc_s_rem
    jmp .say
.def:
    mov si, dc_s_def
    cmp ah, 0FFh
    jne .lead
    or byte [STIK_PAY + DCP_FLAGS], DCF_NOREM
    jmp .say
.lead:
    and byte [STIK_PAY + DCP_FLAGS], ~DCF_NOREM & 0FFh
    mov [STIK_PAY + DCP_LEAD], ah
.say:
    call dc_touch
    mov di, dc_line             ; the toast: its prefix and the lead's words
    call dc_puts
    mov si, bx
    call dc_puts
    mov byte [di], 0
    mov si, dc_line
    jmp dc_toast

; -----------------------------------------------------------------------------
; navigation - every one is the UI task with the lock held
; -----------------------------------------------------------------------------

; dc_kup / dc_kdown / dc_kright / dc_kleft - the arrows: dc_step by a week
; or a day
dc_kup:
    mov al, -7
    jmp dc_step
dc_kdown:
    mov al, 7
    jmp dc_step
dc_kright:
    mov al, 1
    jmp dc_step
dc_kleft:
    mov al, 0FFh

; dc_step - the selection AL days forward (signed), across a month edge;
; refused past either end of the range
dc_step:
    add al, [dc_sel]            ; AL = the day it lands on, as a signed byte
    cmp al, 1
    jl .back
    cmp al, [dc_dim]
    jg .on
    jmp dc_select               ; inside the month
.back:
    mov ah, al
    mov al, 0FFh
    jmp .go
.on:
    sub al, [dc_dim]            ; ...counted on from the next one's 1st
    mov ah, al
    mov al, 1
.go:
    call dc_mgo                 ; the month before / after, built
    je .x
    cmp al, 1
    je .land
    add ah, [dc_dim]            ; ...counted back from its end
.land:
    mov [dc_sel], ah
    jmp dc_regridb
.x:
    ret

; dc_bprev / dc_bnext - a month back / on (`<` / `>`, PgUp / PgDn, the menu)
dc_bprev:
    mov al, 0FFh
    jmp dc_month
dc_bnext:
    mov al, 1

; dc_month - a month back (AL = 0FFh) or forward (AL = 1); refused at either
; end of the range. The selected day is kept, or clamped to the new length
dc_month:
    call dc_mgo
    je dc_step.x
    mov al, [dc_dim]
    cmp [dc_sel], al
    jbe dc_regridb
    mov [dc_sel], al
    jmp dc_regridb

; dc_mgo - the month on show a month back (AL = 0FFh) or on (AL = 1), and
; built: ZF = 1 refused at either end of the range (nothing changed), else
; ZF = 0. Keeps every register. A month off either end of the year wraps
; to 0 or 13, and XOR 12 is 12 and 1
dc_mgo:
    cmp al, 1
    je .fwd
    call dc_atfirst
    jmp .q
.fwd:
    call dc_atlast
.q:
    je .r
    add [dc_m], al
    jz .w
    cmp byte [dc_m], 13
    jb .b
.w:
    push ax
    cbw
    add [dc_y], ax
    pop ax
    xor byte [dc_m], 12
.b:
    call dc_build
    or al, al                   ; ZF = 0: AL is 0FFh or 1
.r:
    ret

; dc_gotoday - today, on its month
dc_gotoday:
    mov ax, [dc_ty]
    cmp ax, [dc_y]
    jne .other
    mov al, [dc_tm]
    cmp al, [dc_m]
    jne .other
    mov al, [dc_td]
    jmp dc_select
.other:
    call dc_totoday

; dc_regridb - a new month on show, built: the day's list, card 1 from the
; nav down and card 2 (SPEC.md 110.3: <= 8 calls)
dc_regridb:
    call dc_newday
    call dc_arm
    jc .x
    mov di, [dc_geo]            ; card 1 from the nav down
    mov bl, [di + DCG_NAVY]
    xor bh, bh
    dec bx
    dec bx
    xor al, al
    call dc_chgt
    dec dx
    call dc_rrows
    jmp dc_rday
.x:
    ret

; dc_select - select day AL of the month on show: the old day's box and the
; new day's, a sub-rect each, and card 2 (SPEC.md 110.3: <= 5 calls)
dc_select:
    cmp al, [dc_sel]
    je .r
    push ax
    call dc_sidx
    xchg ax, bx                 ; BL = the old index
    pop ax
    mov [dc_sel], al
    call dc_newday
    call dc_arm
    jc .r
    call dc_tidx
    mov bh, al                  ; BH = today's: its box is inverse selected
    mov al, bl                  ; or not, so it is never drawn again here
    call .box
    call dc_sidx
    call .box
    jmp dc_rday
.box:
    cmp al, bh
    je .r
    jmp dc_rbox
.r:
    ret

; dc_newday - the selected day changed: its appointments, from their first;
; an appointment selected on the old day is selected no more; then
; dc_menulive. Keeps every register
dc_newday:
    call dc_arange
    mov byte [dc_atop], 0
    cmp byte [dc_selk], DCS_APPT
    jne dc_menulive
    mov byte [dc_selk], 0

; dc_menulive - the items that state a fact (SPEC.md 47): live, or greyed.
; A greyed item is its live string LESS ONE BYTE - each greyable string has
; MENU_DIS in front of it - so it needs no twin; the reason is the toast a
; key gets (`Calendar is full (48)`), or plain on the card. A store into the
; item array - the kernel reads it when the menu drops (12.2), so nothing is
; re-registered. Keeps everything
dc_menulive:
    pushf
    push ax
    push di
    mov di, dc_i_cal
    mov ax, dc_i_newa           ; full, or typing (the three that would
    cmp byte [STIK_PAY + DCP_NAPPT], DCP_MAXAPPT    ; close the field,
    call .typ                   ; throwing its line away)
    mov ax, dc_i_newt
    cmp byte [STIK_PAY + DCP_NTASK], DCP_MAXTASK
    call .typ
    mov ax, dc_i_edit           ; no item, or typing
    cmp byte [dc_selk], 1
    cmc
    call .typ
    mov ax, dc_i_del            ; no item
    cmp byte [dc_selk], 1
    call .put
    mov ax, dc_i_clr            ; none done
    call dc_anydone
    call .put
    mov ax, dc_i_showt          ; Show Tasks / Reminder / Day: COMPACT's
    cmp byte [dc_pane], 1       ; slot, and on the menu only there (dc_entry)
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
.typ:                           ; CF = 1 room: grey it if the field is open
    cmc
    jc .put
    cmp byte [dc_ed], 1
    cmc
.put:                           ; AX = the live string, CF = 1 grey it
    sbb ax, 0
    mov [di], ax
    inc di
    inc di
    ret

; --- the redraws the edits use (the clip armed by dc_arm) --------------------

; dc_rbox - grid index AL's box alone (0FFh: nothing), a sub-rect. Clobbers
; AX, CX, DX, DI; BX kept (dc_select's)
dc_rbox:
    cmp al, 0FFh
    je .r
    push bx
    mov di, [dc_geo]
    cbw                         ; (an index, 0..41)
    mov bl, 7
    div bl                      ; AL = the week, AH = the column
    mov cl, ah
    mul byte [di + DCG_WP]
    add al, [di + DCG_W0]
    adc ah, 0
    sub al, [di + DCG_BOXT]
    sbb ah, 0
    xchg ax, bx                 ; BX = the box's first row
    mov dl, [di + DCG_BOXH]
    xor dh, dh
    add dx, bx
    dec dx
    mov al, cl                  ; the column: a field is THREE bytes, and
    add al, al                  ; the box's first pixel is in its first
    add al, cl                  ; byte and its last in its fourth (the %if
    mov cl, al                  ; beside DC_FLDW), so CL = the first byte...
    add al, 3
    mov ch, al                  ; ...and CH the last
    xor al, al
    call dc_rrect
    pop bx
.r:
    ret

; -----------------------------------------------------------------------------
; the edits. Every one is the UI task with the lock held, and each draws what
; it changed through dc_arm (nothing when the window is covered: the next
; paint shows the state as it is). The ones a handler reaches with the field
; shut by construction (dc_onkey routes every key to the field while it is
; open, dc_oncmd closes it first, dc_oncard only places its caret) do not ask
; dc_busy again; dc_plus and dc_newt are a button's, whose release can come
; after a key opened the field, and do
; -----------------------------------------------------------------------------

; dc_touch - an edit: a save owed (stik_touch), held to the next quiet poll
; (dc_tick). Every register and the flags kept
dc_touch:
    mov byte [dc_tq], 1
    jmp stik_touch

; dc_busy - CF = 1 nothing may be changed now: the field is open. Every
; register kept
dc_busy:
    cmp byte [dc_ed], 1         ; CF = 1 only for 0...
    cmc                         ; ...so CF = 1 for any mode
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

; dc_release - Esc: the bar and the keys back to the front application
; (OSAPI_WM_DESK RELEASE, SPEC.md 11.103.4) - and on kern_small, where there
; is none, nothing selected, as v1
dc_release:
%ifndef STIK_DESKONLY
    test byte [stik_wf], STIK_W_DESK
    jz .sel
%endif
    mov bx, [dc_win]
    mov al, 3
    call OSAPI_WM_DESK
.r:
    ret
%ifndef STIK_DESKONLY
.sel:
    xor bx, bx
    jmp dc_selset
%endif

; dc_pswap - COMPACT's slot: the next page (Tab, a press on its heading, the
; menu). The user's paging is the user's: a ringing reminder's hold on the
; slot is given up. Nothing on TALL / MID, which show all three cards
dc_pswap:
    call dc_compact
    jne dc_release.r
    mov al, [dc_pane]
    inc ax
    cmp al, 3
    jb .s
    xor al, al
.s:
    mov ah, 0FFh                ; [dc_pane0] 0FFh
    mov [dc_pane], ax
    jmp dc_mlday

; dc_apager / dc_tpager - `+N more`: the list's next page, the last page's
; the first
dc_apager:
    mov bx, [dc_geo]
    mov al, [bx + DCG_AN]
    mov di, dc_atop
    mov ah, [dc_acnt]
    jmp dc_pager

dc_tpager:
    mov bx, [dc_geo]
    mov al, [bx + DCG_TN]
    mov di, dc_ttop
    mov ah, [STIK_PAY + DCP_NTASK]
dc_pager:                       ; AL = the lines, [DI] the top, AH the items
    dec ax                      ; (AL >= 1)
    add al, [di]
    cmp al, ah
    jb .s
    xor al, al
.s:
    mov [di], al
    jmp dc_armday

; dc_newa / dc_newt - the field, empty, for a new appointment on the selected
; day / a new task; refused with the reason at a full table (SPEC.md 110.3)
dc_newt:
    call dc_busy
    jc dc_plus.r
    mov al, DCE_NEWT
    cmp byte [STIK_PAY + DCP_NTASK], DCP_MAXTASK
    mov si, dc_s_tfull
    jmp dc_newa.q

dc_newa:
    mov al, DCE_NEWA
    cmp byte [STIK_PAY + DCP_NAPPT], DCP_MAXAPPT
    mov si, dc_s_afull
.q:                             ; CF = 1 room
    jb dc_fopen0
    jmp dc_toast

; dc_plus - `+`: the field for EITHER (the parse decides) - or, on COMPACT's
; tasks page, for a task
dc_plus:
    call dc_compact
    jne .any
    cmp byte [dc_pane], 1
    je dc_newt
.any:
    call dc_busy
    jc .r
    mov al, DCE_NEW
    jmp dc_fopen0
.r:
    ret

; dc_edit - the field holding the selected item (Enter, a second press, Edit
; Item)
dc_edit:
    mov ax, [dc_selk]           ; AL = the kind, AH = the index
    mov [dc_edi], ah
    mov di, dc_ebuf
    cmp al, DCS_APPT
    xchg al, ah                 ; AL = the index (no flags)
    je .appt
    cmp ah, DCS_TASK
    jne dc_plus.r
    call dc_tptr                ; a task: its text
    add si, DCT_TEXT
    mov cx, DCT_TMAX
    mov bl, DCE_EDT
    jmp .cp
.appt:
    call dc_aptr                ; an appointment: `HH:MM text`, or the text
    mov ax, [si + DCA_TIME]
    cmp ax, 24 * 60
    jae .txt
    call dc_tfmt
    mov byte [di], ' '
    inc di
.txt:
    add si, DCA_TEXT
    mov cx, DCA_TMAX
    mov bl, DCE_EDA
.cp:
    call dc_copyz
    mov al, bl
    jmp dc_fopen

; dc_fopen0 - dc_fopen, empty. dc_fopen - open the field, mode AL (DCE_*),
; holding what dc_ebuf holds, in card 2's heading row - on COMPACT with the
; page its list is on
dc_fopen0:
    mov byte [dc_ebuf], 0
dc_fopen:
    mov [dc_ed], al
    call dc_menulive            ; the three that would close it grey
    mov word [dc_ln + LN_BUF], dc_ebuf
    mov word [dc_ln + LN_MAX], DC_ESZ
    cmp al, DCE_NEWT
    jb .m
    cmp al, DCE_EDT
    ja .m
    mov byte [dc_ln + LN_MAX], DCT_TMAX + 1     ; (its high byte is 0)
.m:
    mov word [dc_ln + LN_FOCUS], 1  ; focused, and LN_DIS 0
    mov si, dc_ln
    call os88line_resync        ; the caret at the end of what is there
    call dc_compact
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
    mov al, ah
    mov ah, 0FFh                ; [dc_pane0] 0FFh
    mov [dc_pane], ax
    jmp dc_mlday                ; the page, and the field over its heading
                                ; (dc_fover) - its heading not composed
.a:
    jmp dc_armfd

; dc_askdel - Del / Delete Item, TWICE: the first arms the latch and says so,
; and a second on the very next event, with the same item selected, deletes
; it (SPEC.md 110.3). Every event handler ages the latch first (dc_age), so
; anything in between - a key, a press, another command - disarms it
dc_askdel:
    mov ax, [dc_selk]           ; AL = the kind, AH = the index
    or al, al
    jz .r
    cmp byte [dc_ask], 1        ; armed by the event before this one...
    jne .arm
    cmp ax, [dc_delk]           ; ...for this same item
    je dc_delans
.arm:
    mov byte [dc_ask], 2
    mov [dc_delk], ax
    mov si, dc_s_qdel
    jmp dc_toast
.r:
    ret

; dc_delans - the second Delete (dc_askdel's, AX = [dc_delk]): the item out
dc_delans:
    mov byte [dc_ask], 0
    xchg al, ah                 ; AL = the index, AH = the kind
    cmp ah, DCS_APPT
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
    call dc_aview0              ; ...and the page holding the top NOW
    call dc_build               ; the day's dot
    call dc_menulive
    call dc_arm
    jc .c3
    call dc_sidx
    call dc_rbox
    call dc_rday
.c3:
    mov byte [dc_c3f], 1
    jmp dc_c3sync
.task:
    call dc_tdel
    call dc_fixsel
    call dc_touch
    mov al, [dc_ttop]           ; a page that emptied: the first...
    cmp al, [STIK_PAY + DCP_NTASK]
    jb .tk
    xor al, al
.tk:
    call dc_tview               ; ...and the page holding the top NOW
    jmp dc_mlday

; dc_clear - Clear Done Tasks: the checked ones out, the list from its top
dc_clear:
    call dc_tclear              ; AL = how many went
    or al, al
    jz .r
    cmp byte [dc_selk], DCS_TASK
    jne .k
    mov byte [dc_selk], 0
.k:
    mov byte [dc_ttop], 0
    call dc_touch
    jmp dc_mlday
.r:
    ret

; dc_kspace - Space: the selected task checked or not
dc_kspace:
    mov ax, [dc_selk]
    cmp al, DCS_TASK
    jne dc_clear.r
    mov al, ah

; dc_toggle - task AL checked or not (Space, a press on its box): the box
; redrawn, ONE sub-rect BLIT1 and nothing else (SPEC.md 110.3)
dc_toggle:
    call dc_tptr
    xor byte [si + DCT_FLAGS], DCT_DONE
    call dc_touch
    call dc_menulive
    call dc_arm
    jc dc_rtbox.x

; dc_rtbox - task AL's box alone, when it is on show. Clobbers AX-DI
dc_rtbox:
    call dc_compact
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
    cbw
    add dx, ax
    dec dx
    add al, 7                   ; its bytes: from x 8
    mov ch, al
    mov cl, 3
    shr ch, cl
    mov cl, 1
    mov al, 1
    jmp dc_rrect
.x:
    ret

; -----------------------------------------------------------------------------
; dc_tick - STIK_ONTICK, every 36 ticks (the UI task, lock held, no clip): a
; new DATE recomposes today's page, today's old box and new, card 2's
; heading and card 3. Then the reminders' minute (dcrem.inc), and the second
; note of a chirp one beat after the first. stik_beat banks AX-DI and ES
; round it, and reads none of them after: they are clobbered
; out: CF = 1 HOLD the save owed (os88stik.inc): the entry field is open, or
;      an edit landed since the last poll - a save is seconds of disk with
;      the lock held (SPEC.md 110.6), so it waits for a quiet poll and a burst
;      of edits is one save
; -----------------------------------------------------------------------------
dc_tick:
    call dc_unclip
    xor ax, ax
    xchg ax, [dc_ltp]           ; the launch's summary (dc_missed), said HERE:
    or ax, ax                   ; the first registration writes SYSTEM.CFG
    jz .ch                      ; on this same beat, before us, and CTRL.DRV
    xchg ax, si                 ; says `Settings Saved` for it - from the
    call dc_toast               ; entry proc the line was gone in seconds
    jmp .hold                   ; (SPEC.md 110.3) - and nothing else this beat
.ch:                            ; (AX = 0)
    xchg al, [dc_chirp]
    or al, al
    jz .c
    mov ax, 660                 ; the chirp's second note, four ticks after
    mov cx, 4                   ; its first (stik_nx)
    mov dl, 40h
    call OSAPI_SND_TONE
.c:
    call dc_tidx                ; the old today, if it is on show
    mov cl, al
    call dc_today               ; CF = 1 the date changed
    jnc .min
    or word [dc_lnow], -1       ; a new day: the minute's work for it
    mov byte [dc_c3f], 1        ; ...and card 3's NEXT is another day's
    call dc_arm
    jc .min                     ; not visible: the next paint draws it
    xor ax, ax                  ; today's page: card 1's header block
    cwd
    mov bx, ax
    mov di, [dc_geo]
    mov dl, [di + DCG_HDRB]
    call dc_rrows               ; (CX kept)
    mov al, cl
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
    xor al, al
    xchg al, [dc_tq]            ; an edit this poll...
    or al, [dc_ed]              ; ...or the field open: CF = 1 for
    neg al                      ; either
    ret

; dc_oncmd's Calendar items, in the menu's order
dc_cmds:    dw dc_newa, dc_newt, dc_edit, dc_askdel, dc_clear, dc_gotoday
            dw dc_bprev, dc_bnext, dc_pswap
dc_cmds_end:

; dc_onkey's keys: a byte and the routine it jumps to, in three lists each
; ended by a 0 - by SCANCODE, by CHARACTER, by the character EITHER CASE
; (OR 20h) - looked up in that order
%macro DC_KEY 2
            db %1
            dw %2
%endmacro
dc_keys:
    DC_KEY KSC_LEFT, dc_kleft
    DC_KEY KSC_RIGHT, dc_kright
    DC_KEY KSC_UP, dc_kup
    DC_KEY KSC_DOWN, dc_kdown
    DC_KEY KSC_PGUP, dc_bprev
    DC_KEY KSC_PGDN, dc_bnext
            db 0
    DC_KEY 13, dc_edit
    DC_KEY 27, dc_release
    DC_KEY 9, dc_pswap
    DC_KEY ' ', dc_kspace
            db 0
    DC_KEY 't', dc_gotoday
    DC_KEY 'n', dc_newa
    DC_KEY 'k', dc_newt
    DC_KEY 's', dc_snooze
    DC_KEY 'd', dc_dismiss
    DC_KEY ',', dc_bprev        ; '<' and '>', shifted or not
    DC_KEY '<', dc_bprev
    DC_KEY '.', dc_bnext
    DC_KEY '>', dc_bnext
            db 0

; --- the window (SPEC.md 110.3, 110.11) --------------------------------------
dc_tpl:
    dw 375, 42, 178, 357        ; stik_load sizes and places it (STIK_RIGHT)
    dw dc_title, dc_paint, dc_onkey, 0

dc_title:   db 'Desk '                 ; ...whose tail is the Calendar
dc_m_cal:   db 'Calendar', 0           ; menu's title

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
DCM_TODAY   equ 5               ; ...the three that draw a month
DCM_PREV    equ 6
DCM_NEXT    equ 7
DCM_PANE    equ 8               ; ...and COMPACT's page, LAST: TALL and MID
DCM_N       equ 9               ; show both lists and drop it (dc_entry)
%if (dc_cmds_end - dc_cmds) / 2 != DCM_N
  %error "deskcal: dc_cmds is the Calendar menu's items, in order"
%endif
dc_m_rem:   db 'Remind', 0
dc_i_cal:   dw dc_i_newa, dc_i_newt, dc_i_edit - 1, dc_i_del - 1
            dw dc_i_clr - 1, dc_i_today, dc_i_prev, dc_i_next, dc_i_showt
            db MENU_DIS                 ; each greyable item's MENU_DIS: its
dc_i_newa:  db 'New Appointment  N', 0  ; greyed form is the address before
            db MENU_DIS                 ; it (dc_menulive, SPEC.md 47)
dc_i_newt:  db 'New Task  K', 0
            db MENU_DIS
dc_i_edit:  db 'Edit Item  Enter', 0
            db MENU_DIS
dc_i_del:   db 'Delete Item  Del', 0
            db MENU_DIS
dc_i_clr:   db 'Clear Done '
dc_s_tasksh: db 'Tasks', 0         ; (COMPACT's page name: the tail)
dc_i_today: db 'Go to Today  T', 0
dc_i_prev:  db 'Previous Month', 0
dc_i_next:  db 'Next Month', 0
dc_i_showt: db 'Show Tasks  Tab', 0
dc_i_showr: db 'Show Reminder  Tab', 0
dc_i_showd: db 'Show Day  Tab', 0
dc_i_rem:   dw dc_r0, dc_r1, dc_r2, dc_r3, dc_r4, dc_r5
dc_r0:      db 'At the Time', 0
dc_r2:      db '1'                     ; (dc_r1 is dc_r2's tail)
dc_r1:      db '5 Min Early', 0
dc_r3:      db '30 Min Early', 0
dc_r4:      db '1 Hour Early', 0
dc_r5:      db 'No '
dc_s_remh:  db 'Reminder', 0       ; (COMPACT's page name: the tail)
dc_leads:   db 0, 5, 15, 30, 60, 0FFh   ; ...their leads; 0FFh none
dc_rlbl:    dw dc_l0, dc_l1, dc_l2, dc_l3, dc_l4, dc_l5
dc_l0:      db 'at the time', 0     ; ...and the toast's words for them
dc_l2:      db '1'                     ; (dc_l1 is dc_l2's tail)
dc_l1:      db '5 min early', 0
dc_l3:      db '30 min early', 0
dc_l4:      db '1 hour early', 0
dc_l5:      db 'off', 0
dc_s_rem:   db 'Reminder ', 0       ; the selected appointment's...
dc_s_def:   db 'Default: ', 0       ; ...or a new one's (<= 24 in all)
%defstr DC_NAPPT_S DC_NAPPT
%defstr DC_NTASK_S DC_NTASK
dc_s_afull: db 'Calendar is full (', DC_NAPPT_S, ')', 0
dc_s_tfull: db 'Task list is full (', DC_NTASK_S, ')', 0
dc_s_long:  db 'At most 25 letters', 0
dc_s_tlong: db 'A task: at most 23', 0
dc_s_qdel:  db 'Delete again to confirm', 0
dc_s_dmg:   db 'Calendar file damaged', 0

dc_s_about: db 'Desk Calendar for os8088', 0

%include "dcdate.inc"
%include "dcdata.inc"
%include "dcrem.inc"

%define OS88UI_MACROS           ; the UI_* macros os88line.inc draws with and
                                ; no control: the cards draw their own, and
                                ; there is no alert and no About card
                                ; (SPEC.md 110.3, 110.11)
%define OS88UI_1BPP             ; OSAPI_SAVEU_1BPP: a greyed caption keeps its ink (SPEC.md 13.8.11)
%include "os88ui.inc"
%define OS88LINE_NOSET          ; the field is filled in place and resynced,
                                ; never replaced (apps size pass 2b)
%include "os88line.inc"
%include "os88gfx.inc"
%include "os88stik.inc"
%include "dcdraw.inc"           ; AFTER os88stik.inc: STIK_LAYOUT is its macro

%if DC_FX1 + LN_INSET > LN_CELL
  %error "deskcal: the entry field's inset must leave its pen on face x 8 (dc_frect)"
%endif

%if OS88LINE_SZ != 20
  %error "deskcal: its bss holds a 20-byte field block"
%endif

    align 16, db 0              ; os88stik.inc's read lands on a paragraph
    OS88_BSS STIK_BSS_END
    OS88_IMAGE_END

; what the code above leans on, from this file's bss and the others' equs
%if dc_td - dc_tm != 1 || dc_sel - dc_m != 1 || dc_pane0 - dc_pane != 1 || dc_seli - dc_selk != 1 || dc_deli - dc_delk != 1
  %error "deskcal: a word load or store spans two bytes that are no longer adjacent"
%endif
%if dc_hx0 - dc_hb != dc_lx0 - dc_lact || dc_hx1 - dc_hb != dc_lx1 - dc_lact || dc_hy0 - dc_hb != dc_ly0 - dc_lact || dc_hy1 - dc_hb != dc_ly1 - dc_lact || dc_hy1 + 2 - dc_hb != 7
  %error "deskcal: dc_oncard copies dc_hb.. to dc_lact.. as seven bytes"
%endif
%if DCH_DAY != 1 || DCH_BTN != 2 || DCH_ALINE != 3 || DCH_TBOX != 4 || DCH_TTEXT != 5
  %error "deskcal: dc_oncard counts the hit kinds down from DCH_DAY = 1"
%endif
%if DC_FLD0 + 2 + 6 * DC_FLDW + DC_BOXW - 1 > 255 || DC_ALLDAY != 0FFFFh || LN_DIS != LN_FOCUS + 1
  %error "deskcal: dc_rbox's byte arithmetic, dc_fenter's inc or dc_fopen's word store"
%endif
