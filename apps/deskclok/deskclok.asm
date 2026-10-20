; =============================================================================
; os8088 - apps/deskclok/deskclok.asm
;
; DESK CLOCK - the third STICKY APP (SPEC.md 110, its own section 110.14): a
; clock pinned to the desktop, in nine faces - three dials (Classic, Memphis
; '80, Neon '90) and six digital ones (Mono Digital, Casio LCD, Casio VGA,
; Red LED, Amber LED, Flip Clock) - at three sizes, with seconds or not, 12-
; or 24-hour, and a daily alarm with Snooze and Dismiss. It ships in
; STIKAPPS\ on the system disk and includes apps/os88stik.inc, which owns its
; place, its state file (SYSTEM\APPDATA\DESKCLOK.DAT) and its row in the
; desktop configuration.
;
; THREE WINDOWS, ONE PROGRAM:
;
;   THE CLOCK      the Sticky App's own window - a DESK window (SPEC.md
;                  11.103) in os88stik.inc's widget treatment, its size the
;                  settings' (STIK_VARFACE): the face and, unless `Clock face
;                  only`, the info panel under it (dkwid.inc)
;   THE SETTINGS   `Desk Clock Settings`, a titled second window: the three
;                  check boxes, the size, the gallery of nine faces, and
;                  Cancel / Apply / OK. A change previews on the desk at once
;                  (dkset.inc)
;   THE ALARM      `Alarm`, a small titled second window: on or off, the
;                  time, the label (dkalm.inc)
;
;   deskclok.asm  this: header, card, payload, bss, entry, the tick, keys,
;                 menus, the alarm's ringing
;   dkdraw.inc    the composer: a 1- or 4-plane band, spans, glyphs, shapes
;   dkface.inc    the nine faces, as programs, and the time and date
;   dkwid.inc     the clock on the desk: layout, paint, presses, redraws
;   dkset.inc     the settings window
;   dkalm.inc     the alarm window
;
; COLOUR is composed in four planes and put down with OSAPI_GFX_BLITP on a
; VGA or EGA; a 1bpp adapter gets the same faces in black, white and three
; greys (dkdraw.inc's dk_lc). Nothing is drawn twice: each slab is composed
; in RAM, desktop and all, and blitted once (SPEC.md 110.14.4).
; =============================================================================

%ifndef APP_SMALL               ; kern_big's: the Sticky App code is the ONE
%define STIK_LIB                ; copy in DESK.DRV (SPEC.md 110.15). Before
%endif                          ; the header: OS88_STIKCARD_END lays its block
%include "os88api.inc"

    OS88_HEADER 'Desk Clock', dk_entry, OS88_F_ICON | OS88_F_DESK | OS88_F_STIK

; A clock's dial at ten past ten: also the Sticky App row's picture
; (STIK_ICON), so a row the boot could not launch becomes a desktop icon that
; looks like what it was (SPEC.md 110.4).
    OS88_ICON16
    dw 0x0000, 0x0FF0, 0x1FF8, 0x3FFC, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x3FFC, 0x1FF8, 0x0FF0, 0x0000
    dw 0x0000, 0x0FF0, 0x1898, 0x300C, 0x6006, 0x4C1A, 0x4672, 0x43C2
    dw 0x6086, 0x4002, 0x4002, 0x6006, 0x300C, 0x1898, 0x0FF0, 0x0000
    OS88_ICON16_END

; The Sticky Apps menu's row for us (SPEC.md 110.12): a dial at ten past ten.
    OS88_STIKCARD 'Desk Clock', 'Nine faces & an alarm'
    OS88_STIKROW '................................'
    OS88_STIKROW '..............####..............'
    OS88_STIKROW '..........############..........'
    OS88_STIKROW '........####....#...####........'
    OS88_STIKROW '.......###......#.....###.......'
    OS88_STIKROW '......##.##.....#....##.##......'
    OS88_STIKROW '.....##...#..........#...##.....'
    OS88_STIKROW '....##....................##....'
    OS88_STIKROW '...##......................##...'
    OS88_STIKROW '...###....................###...'
    OS88_STIKROW '..##.##................####.##..'
    OS88_STIKROW '..##.....###.........#####..##..'
    OS88_STIKROW '..#......####......######....#..'
    OS88_STIKROW '..#.......#####...#####......#..'
    OS88_STIKROW '.##........##########........##.'
    OS88_STIKROW '.##..........#######.........##.'
    OS88_STIKROW '.#####........####........#####.'
    OS88_STIKROW '.##..........................##.'
    OS88_STIKROW '..#..........................#..'
    OS88_STIKROW '..#..........................#..'
    OS88_STIKROW '..##........................##..'
    OS88_STIKROW '..##.##..................##.##..'
    OS88_STIKROW '...###....................###...'
    OS88_STIKROW '...##......................##...'
    OS88_STIKROW '....##....................##....'
    OS88_STIKROW '.....##...#..........#...##.....'
    OS88_STIKROW '......##.##.....#....##.##......'
    OS88_STIKROW '.......###......#.....###.......'
    OS88_STIKROW '........####....#...####........'
    OS88_STIKROW '..........############..........'
    OS88_STIKROW '..............####..............'
    OS88_STIKROW '................................'
    OS88_STIKCARD_END

; --- the payload v1 (SPEC.md 110.14.5) -----------------------------------------
; tools/os88stik.py's DESKCLOK_* are the same numbers; t_stik holds them equal
DKP_FACE    equ 0               ; db the face, 0..8 (dkface.inc's order)
DKP_FLAGS   equ 1               ; db DKF_*
DKF_ONLY    equ 1               ; `Clock face only`
DKF_SECS    equ 2               ; seconds shown
DKF_H24     equ 4               ; 24-hour
DKF_ALARM   equ 8               ; the alarm is set
DKF_INIT    equ 80h             ; the defaults were made once
DKP_SIZE    equ 2               ; db 0 Small, 1 Medium, 2 Large
DKP_AH      equ 3               ; db the alarm's hour, 0..23
DKP_AM      equ 4               ; db ...its minute
DKP_LABEL   equ 5               ; 17: its label, 0..16 characters and a NUL
DK_LABMAX   equ 16
DKP_LEN     equ 32
DKP_VER     equ 1
DKF_N       equ 9               ; the faces (dkface.inc's order)
DKF_ANALOG  equ 3               ; 0..2 have hands; 3..8 are digital

; --- the Sticky App (apps/os88stik.inc, SPEC.md 110.5, 110.11, 110.14.1) -------
%define STIK_DATNAME 'DESKCLOK.DAT'
%define STIK_CAP     'Desk Clock'
%define STIK_ISOPEN  'Desk Clock is open'
%define STIK_DMGREPLACE         ; a damaged file is replaced by the first save
                                ; (SPEC.md 110.5.2)
%define STIK_ONTICK  dk_tick
%define STIK_ONCLOSE dk_onclose
STIK_PAYLEN equ DKP_LEN
STIK_PAYVER equ DKP_VER
STIK_ICON   equ 32
STIK_BSS    equ DK_BSS          ; the include's state follows ours
%define STIK_WIDGET
%ifndef APP_SMALL               ; kern_big's build: a desk widget or nothing.
%define STIK_DESKONLY           ; The small build (make small) keeps the
%endif                          ; titled fallback and the worker arm (110.7)
%define STIK_VARFACE            ; the width and the card are the settings'
%define STIK_SIZEUP   dk_sizeup
%define STIK_COLOR              ; the titled fallback's save-under: 4 planes
%define STIK_TAG      'Desk Clock'      ; = the title
%define STIK_FACE_W 176         ; the WIDEST face (a %define: %ifndef)
%define STIK_RIGHT    616       ; the left of the desk, clear of Sticky
                                ; Note's 472 and the calendar's 264, with the
                                ; settings window's room right of it
%define STIK_LAYOUTS  dk_lays
%define STIK_PAINTCARD dk_pcard
%define STIK_ONCARD   dk_oncard
%define STIK_ONCARDUP dk_oncardup
%define GFXE_TEXT
%define GFXE_BITS
%define GFXE_BAND_W   176       ; the tag's band: the include's, in ours
%define GFXE_BAND_H   16
%define GFXE_BAND_BUF dk_band
%define GFXE_NOBUTIL            ; the tag papers and blits its own band (the
                                ; include's stik_blit): no bounds, pixel or
                                ; gfxe_bput - the Desk Calendar's (SPEC.md
                                ; 5.12.9)

DKP         equ STIK_PAY
DK_BANDSZ   equ 4608            ; the composer's band: four planes of a
DK_SLABMAX  equ 64              ; 368-px window x 25 rows, or of the clock
                                ; x 52; one plane takes more rows, up to 64
DK_SETW     equ 368             ; the settings window's content width

KSC_PGUP    equ 0x49
KSC_PGDN    equ 0x51

; --- bss: OURS FIRST, at fixed offsets ----------------------------------------
%assign DKB 0
%macro DKV 2                    ; name, bytes
%1 equ os88_image_end + DKB
%assign DKB DKB + %2
%endmacro
    ; the composer (dkdraw.inc)
    DKV ck_st, 2
    DKV ck_ps, 2
    DKV ck_y0, 2
    DKV ck_sh, 2
    DKV ck_cx0, 2
    DKV ck_cx1, 2
    DKV ck_ox, 2
    DKV ck_oy, 2
    DKV ck_pp, 2
    DKV ck_pat, 8
    DKV ck_gnd, 8
    DKV ck_fseg, 2
    DKV ck_foff, 2
    DKV ck_bw, 2
    DKV ck_bh, 2
    DKV ck_rx, 2
    DKV ck_ry, 2
    DKV dk_gl, 8
    DKV dk_lx0, 2               ; ck_line's line: its ends' x...
    DKV dk_lx1, 2
    DKV dk_ly0, 2               ; ...their rows, doubled
    DKV dk_ly1, 2
    DKV dk_lk, 2                ; ...the slope, 8.8 a doubled row, and two
    DKV dk_lk2, 2
    DKV dk_lhu, 2               ; ...the brush's rows above and below (doubled)
    DKV dk_lhd, 2
    DKV dk_lta, 2               ; ...the row's two ends: their doubled rows
    DKV dk_ltb, 2
    DKV dk_lfa, 2               ; ...and their 8.8 offsets
    DKV dk_lfb, 2
    DKV dk_lend, 2              ; ...the last row to draw
    DKV ck_mw, 4                ; ck_mrow's pattern, bit 31 the leftmost
    DKV dk_mb, 5                ; ...shifted onto the byte grid
    DKV dk_smx, 2               ; the panel's small `:07`, as last drawn
    DKV dk_gx, 2                ; ck_g1's first byte...
    DKV dk_gsh, 1               ; ...and its shift
    DKV dk_eend, 2              ; ck_ell's last j, past it
    DKV dk_rend, 2              ; ck_rrect's last row, past it
    DKV dk_rw, 2                ; ck_rect's bytes - 1...
    DKV dk_rm, 2                ; ...its masks, first and last
    DKV dk_rpar, 1              ; ...its first row's parity
    DKV ck_hwg, 2               ; ck_rootg's guess: the last row's root
    DKV dk_tp, 12
    DKV ck_bseg, 2              ; the band's segment and offset: dk_band, or
    DKV ck_buf, 2               ; the face cache's claim (dkwid.inc)
    DKV dk_cseg, 2              ; the face cache: its claim, 0 none...
    DKV dk_cst, 2               ; ...its stride and plane
    DKV dk_cps, 2
    DKV dk_cgnd, 8              ; ...the ground it was composed on
    DKV dk_ckb, 1               ; ...the KB held
    DKV dk_cok, 1               ; ...it is the face as it stands
    DKV dk_cnp, 1               ; ...its planes
    DKV dk_cpar, 1              ; ...its ground's parity
    DKV dk_fmode, 1             ; dk_face: 0 every op, 1 the static, 2 the rest
    DKV ck_crow, 1              ; ck_clear's rows: the slab's live ones
    DKV ck_lrow, 2              ; ck_span's last row, and its base (8000h:
    DKV ck_lrdi, 2              ; none - a band that changes says so)
    DKV dk_shf, 2               ; the band's full height ([ck_sh] is cut to
                                ; a slab's live rows while it is composed)
    DKV ck_np, 1
    DKV ck_par, 1
    DKV ck_gpar, 1
    DKV ck_ffst, 1
    DKV ck_flst, 1
    DKV ck_gsx, 1
    DKV ck_gsy, 1
    DKV ck_gbold, 1
    ; the faces (dkface.inc)
    DKV dk_fw, 2
    DKV dk_fh, 2
    DKV dk_cx, 2
    DKV dk_cy, 2
    DKV dk_R, 2
    DKV dk_Ry, 2
    DKV dk_ys, 2                ; the screen's aspect: rows a px across, x256
    DKV dk_tx, 2
    DKV dk_ty, 2
    DKV dk_tw, 2
    DKV dk_th, 2
    DKV dk_dw, 2
    DKV dk_gap, 2
    DKV dk_cur, 2
    DKV dk_stx, 2
    DKV dk_sty, 2
    DKV dk_smid, 2
    DKV dk_dig, 6
    DKV dk_numf, 1
    DKV dk_tn, 1
    DKV dk_tf1, 1
    DKV dk_tf2, 1
    DKV dk_tfl, 1
    DKV dk_tst, 1
    DKV dk_tsty, 1
    DKV dk_tlit, 1
    DKV dk_tdim, 1
    DKV dk_taux, 1
    DKV dk_tam, 1
    DKV dk_tcol, 1
    DKV dk_d, 1
    DKV dk_sbits, 1
    DKV dk_sbit, 1
    ; the clock
    DKV dk_year, 2
    DKV dk_mon, 1
    DKV dk_day, 1
    DKV dk_hh, 1
    DKV dk_mm, 1
    DKV dk_ss, 1
    DKV dk_dow, 1
    DKV dk_rtc, 1
    DKV dk_line, 24
    DKV dk_small, 8
    ; the clock on the desk (dkwid.inc)
    DKV dk_win, 2
    DKV dk_lay, 12              ; a STIK_LAYOUT entry (STIKL_SZ, checked below)
    DKV dk_faw, 2
    DKV dk_fah, 2
    DKV dk_fox, 2
    DKV dk_cw, 2
    DKV dk_ch, 2
    DKV dk_pix, 2
    DKV dk_pt, 2
    DKV dk_ph, 2
    DKV dk_tyT, 2
    DKV dk_tyD, 2
    DKV dk_tyR, 2
    DKV dk_tyA, 2
    DKV dk_tyS, 2
    DKV dk_r0, 2
    DKV dk_r1, 2
    DKV dk_t0, 2
    DKV dk_t1, 2
    DKV dk_gsx, 2
    DKV dk_gsy, 2
    DKV dk_gsv, 8
    DKV dk_hnew, 24             ; the hands' boxes now...
    DKV dk_hold, 24             ; ...and on the glass
    DKV dk_hang, 3              ; the hands' angles now...
    DKV dk_hshown, 3            ; ...and on the glass
    DKV dk_clkt, 2              ; the last press: its tick and place
    DKV dk_clkx, 2
    DKV dk_clky, 2
    DKV dk_lfire, 2             ; the minute of the day the alarm last rang
    DKV dk_rbeat, 2             ; the beats it has rung
    DKV dk_kind, 1              ; 0 a dial, 3 digital
    DKV dk_pan, 1               ; 0 no panel, 1 the full one, 2 the alarm's
    DKV dk_bsy, 1               ; the big time's rows a glyph row
    DKV dk_dbpp, 1              ; the window's display's bits a pixel
    DKV dk_gok, 1               ; a ground sample is held
    DKV dk_gq, 1
    DKV dk_press, 1             ; the panel control a press went to
    DKV dk_ring, 1              ; 0 quiet, 1 ringing, 2 rung out (shown)
    DKV dk_snz, 1               ; snoozed...
    DKV dk_snh, 1               ; ...until this hour
    DKV dk_snm, 1               ; ...and minute
    DKV dk_lsec, 1              ; the second the glass shows
    DKV dk_mono, 1              ; the primary is 1bpp
    ; THE FORMS (dkset.inc): the settings window and the alarm window, each
    ; a record - its window (0 none), its layout, a button pressed + 1
    ; (DKW_*) - and what only one of them has after it
    DKV dk_swin, 2              ; the settings
    DKV dk_sgeo, 2
    DKV dk_sbtn, 1
    DKV dk_sav, DKP_LEN         ; the payload as last applied
    DKV dk_sbseg, 2             ; a paint's tall band: its claim, 0 none
    DKV dk_st0, 2               ; ...the tile rows: the first's y, the pitch
    DKV dk_spit, 2
    DKV dk_awin, 2              ; the alarm
    DKV dk_ageo, 2
    DKV dk_abtn, 1
    DKV dk_aon, 1               ; the edit: on, then the hour, the minute
    DKV dk_ah, 1                ; and the label as the payload has them
    DKV dk_am, 1                ; (DKP_AH ..), one copy each way
    DKV dk_alab, DK_LABMAX + 1
    DKV dk_afoc, 1              ; the field with the keys (DKAF_*)
    DKV dk_fm, 2                ; the form being drawn or pressed: dk_fset / dk_falm
    DKV dk_wox, 2               ; ...its content's origin on the glass
    DKV dk_woy, 2
    DKV dk_tent, 12             ; a gallery tile's entry, made (dk_tileent)
dk_fset       equ dk_swin
dk_falm       equ dk_awin
DKW_WIN     equ 0
DKW_GEO     equ dk_sgeo - dk_swin
DKW_BTN     equ dk_sbtn - dk_swin
    times -((dk_ageo - dk_awin != DKW_GEO) | (dk_abtn - dk_awin != DKW_BTN)) db 0
DKA_EDIT    equ DKP_LABEL + DK_LABMAX + 1 - DKP_AH  ; dk_ah .. the label's NUL
    times -((dk_am - dk_ah != DKP_AM - DKP_AH) | (dk_alab - dk_ah != DKP_LABEL - DKP_AH)) db 0
%assign DKB (DKB + 15) & ~15
    DKV dk_band, DK_BANDSZ
DK_BSS      equ DKB             ; a multiple of 16
%if DK_BSS % 16
  %error "deskclok: its own bss must end on a paragraph"
%endif

; -----------------------------------------------------------------------------
; dk_entry - the entry proc (DS = CS = ours, ES = KERNEL_SEG, the UI task)
; in:  SI = the loader's name, for os88stik.inc
; out: CF = 0 launched; CF = 1 not (another Desk Clock is up, or no window)
; -----------------------------------------------------------------------------
dk_entry:
    push si
    push di
    call ck_ourband             ; the composer's band, until a cache is made
    call dk_aspect              ; [dk_ys]: STIK_SIZEUP measures with it
    mov word [dk_lfire], 0FFFFh
    mov word [dk_clkt], 8000h
    call dk_now                 ; the clock, before the first paint
    mov di, dk_tpl
    call stik_load              ; the state file, the layout (dk_sizeup),
    jc .out                     ; the template's size and place
    mov si, dk_tpl
    call OSAPI_WM_CREATE        ; BX = the clock, CF = the table is full
    jc .out
    mov [dk_win], bx
    OS88_REGION_MOVABLE dk_ret
    mov al, 1
    call OSAPI_WM_KEEPH         ; a fixed layout: hang over the dock, never cut
    mov si, dk_menus
    call OSAPI_MENU_SET
    mov si, dk_about
    call OSAPI_ABOUT_SET
    call stik_made              ; MAKE, the prefilters, the tick
    call dk_hands               ; what the first paint will draw
    call dk_hkeep
    mov al, [dk_ss]
    mov [dk_lsec], al
    call dk_menulive
    clc
.out:
    pop di
    pop si
dk_ret:
    ret

; dk_aspect - [dk_ys] = the primary's rows a pixel across, x256: 256 on
; VGA's square pixels, 107 on CGA's 640 x 200, 165 on Hercules. [dk_mono]
dk_aspect:
    push ax
    push bx
    push cx
    push dx
    call OSAPI_VIDEO            ; AX = width, BX = height, DH = bits a pixel
    mov cl, 0
    cmp dh, 1
    ja .c
    inc cx
.c:
    mov [dk_mono], cl
    mov cx, ax
    mov ax, bx
    mov dx, 1024 / 4            ; h x 1024 / (3 w), in two steps for range:
    mul dx                      ; h x 256 / (3 w / 4)
    push ax
    mov ax, cx
    mov bx, 3
    push dx
    mul bx
    shr ax, 1
    shr ax, 1
    mov bx, ax
    pop dx
    pop ax
    div bx
    mov [dk_ys], ax
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; dk_check - a payload this build did not write is made the DEFAULTS: Classic,
; Medium, the panel shown, no seconds, the system's 12/24-hour choice, the
; alarm off at 07:00 `Wake up`. A file with no INIT mark is a first launch.
; Keeps everything
; -----------------------------------------------------------------------------
dk_check:
    push ax
    push cx
    push si
    test byte [DKP + DKP_FLAGS], DKF_INIT
    jz .def
    cmp byte [DKP + DKP_FACE], DKF_N
    jae .def
    cmp byte [DKP + DKP_SIZE], 3
    jae .def
    cmp byte [DKP + DKP_AH], 24
    jae .def
    cmp byte [DKP + DKP_AM], 60
    jae .def
    mov si, DKP + DKP_LABEL     ; the label: printable, and its NUL inside
    mov cx, DK_LABMAX + 1
.l:
    lodsb
    or al, al
    jz .ok
    cmp al, ' '
    jb .def
    cmp al, 126
    ja .def
    loop .l
    jmp short .def
.ok:
    pop si
    pop cx
    pop ax
    ret
.def:
    push di
    push es
    push ds
    pop es
    mov di, DKP
    mov cx, DKP_LEN
    xor al, al
    cld
    rep stosb
    mov byte [DKP + DKP_SIZE], 1
    mov byte [DKP + DKP_AH], 7
    mov si, dks_wake
    mov di, DKP + DKP_LABEL
    mov cx, 8
    rep movsb
    pop es
    pop di
    mov al, DKF_INIT | DKF_H24
    test byte [dk_rtc], 2       ; the user chose a 12-hour clock (SPEC.md
    jz .h                       ; 37.96): so does a first launch
    and al, ~DKF_H24 & 0FFh
.h:
    mov [DKP + DKP_FLAGS], al
    jmp short .ok
dks_wake:   db 'Wake up', 0

; -----------------------------------------------------------------------------
; dk_tick - STIK_ONTICK, every beat (the UI task, lock held, no clip): the
; time read; what changed redrawn and nothing more; the alarm; the next beat
; aimed - every 6 ticks while the seconds show or the alarm rings, else at
; the next minute (at most 35 ticks out).
; out: CF = 1 HOLD any save owed: the settings or the alarm window is up, so
;      the payload is a preview
; -----------------------------------------------------------------------------
dk_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    mov ch, [dk_hh]             ; the time on the glass
    mov cl, [dk_mm]
    mov dh, [dk_day]
    call dk_now
    cmp dh, [dk_day]
    jne .full                   ; a new day: the date, everything
    cmp ch, [dk_hh]
    jne .min
    cmp cl, [dk_mm]
    jne .min
    mov al, [dk_ss]
    cmp al, [dk_lsec]
    je .ring
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jz .hands                   ; (the minute hand steps at :30 too)
    call dk_dsecs               ; a second: the seconds and nothing else
    jmp short .hands
.min:
    call dk_alarm               ; a minute: is it time?
    call dk_dmin
.hands:
    cmp byte [dk_kind], 0
    jne .ring
    call dk_hmove
    jmp short .ring
.full:
    call dk_alarm
    call dk_redraw
    call dk_hands
    call dk_hkeep
.ring:
    mov al, [dk_ss]
    mov [dk_lsec], al
    call dk_chirp               ; a note, while it rings
    mov al, 6                   ; the next beat
    cmp byte [dk_ring], 1
    je .nx
    test byte [DKP + DKP_FLAGS], DKF_SECS
    jnz .nx
    mov al, 60
    sub al, [dk_ss]             ; ticks to the minute: (60 - s) x 18.2
    mov ah, 18
    mul ah
    cmp ax, 35
    jbe .lo
    mov ax, 35
.lo:
    cmp al, 2
    jae .nx
    mov al, 2
.nx:
    mov [stik_nx], al
    mov ax, [dk_swin]           ; a window of ours up: the payload is a
    or ax, [dk_awin]            ; preview - hold the save
    neg ax                      ; CF = 1 when either is open
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_dmin - a new minute on the glass: a digital face whole, the panel's
; time under a dial
dk_dmin:
    cmp byte [dk_kind], 0
    jne dk_dface
    jmp dk_dbig

; dk_dsecs - a new second: a digital face's seconds column, or the panel's
; seconds under a dial (whose second hand dk_hmove moves)
dk_dsecs:
    cmp byte [dk_kind], 0
    je dk_dsec
    push ax
    push bx
    push cx
    push dx
    mov ax, [dk_faw]            ; the right 3/10 of the face: the column
    mov cx, 3                   ; of the seconds (and a 12-hour clock's
    mul cx                      ; AM/PM, over them)
    mov cx, 10
    div cx
    neg ax
    add ax, [dk_faw]
    add ax, [dk_fox]
    xor bx, bx
    mov cx, [dk_fox]
    add cx, [dk_faw]
    dec cx
    mov dx, [dk_fah]
    dec dx
    call dk_rrect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_dsec - the panel's `:07` alone: a second that is not a new minute
; changes those three cells under a dial, and nothing else of its row
dk_dsec:
    cmp byte [dk_pan], 1
    jne .r
    push ax
    push bx
    push cx
    push dx
    mov ax, [dk_smx]
    mov cx, ax
    add cx, 3 * 8 - 1
    mov bx, [dk_tyT]
    mov dl, [dk_bsy]            ; the small part sits on the big one's foot
    dec dl
    xor dh, dh
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add bx, dx
    mov dx, bx
    add dx, 7
    call dk_rrect
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; dk_dface - the face again whole
dk_dface:
    push ax
    push bx
    push cx
    push dx
    mov ax, [dk_fox]
    xor bx, bx
    mov cx, ax
    add cx, [dk_faw]
    dec cx
    mov dx, [dk_fah]
    dec dx
    call dk_rrect
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dk_dbig - the panel's big time again (under a dial, with the panel full)
dk_dbig:
    cmp byte [dk_pan], 1
    jne .r
    cmp byte [dk_kind], 0
    jne .r
    push ax
    push bx
    push cx
    push dx
    mov ax, [dk_pix]
    mov cx, ax
    add cx, DK_PANW - 1
    mov bx, [dk_tyT]
    mov dl, [dk_bsy]
    xor dh, dh
    shl dx, 1
    shl dx, 1
    shl dx, 1
    add dx, bx
    dec dx
    call dk_rrect
    pop dx
    pop cx
    pop bx
    pop ax
.r:
    ret

; -----------------------------------------------------------------------------
; THE ALARM: it rings at its minute once a day (and at a snooze's), chirps -
; 880 Hz and 660 Hz, a note a beat - for 30 seconds, and its panel rows show
; `Snooze . Dismiss` until one is picked. A toast says it, as the calendar's
; reminders do (SPEC.md 110.3); nothing is modal and nothing takes the keys
; -----------------------------------------------------------------------------
DK_RINGN    equ 90              ; beats of 6 ticks: about 30 seconds
DK_SNOOZE   equ 5               ; minutes

; dk_alarm - the minute changed: ring when it is the alarm's (or a snooze's)
; and it has not rung this minute. Keeps everything
dk_alarm:
    push ax
    push bx
    push cx
    mov al, [dk_hh]
    mov ah, 60
    mul ah
    add al, [dk_mm]
    adc ah, 0
    cmp ax, [dk_lfire]
    je .x
    mov cx, ax                  ; CX = the minute of the day
    cmp byte [dk_snz], 0
    je .set
    mov al, [dk_snh]
    mov ah, 60
    mul ah
    add al, [dk_snm]
    adc ah, 0
    cmp ax, cx
    je .go
.set:
    test byte [DKP + DKP_FLAGS], DKF_ALARM
    jz .x
    mov al, [DKP + DKP_AH]
    mov ah, 60
    mul ah
    add al, [DKP + DKP_AM]
    adc ah, 0
    cmp ax, cx
    jne .x
.go:
    mov [dk_lfire], cx
    mov byte [dk_snz], 0
    mov word [dk_rbeat], 0
    mov byte [dk_ring], 1
    call dk_ringtoast
    call dk_relay               ; the panel's rows grow for Snooze . Dismiss
    call dk_menulive
.x:
    pop cx
    pop bx
    pop ax
    ret

; dk_chirp - one note a beat while ringing; rung out after DK_RINGN beats
; (its rows stay until Snooze or Dismiss). Keeps everything
dk_chirp:
    cmp byte [dk_ring], 1
    jne .r
    push ax
    push cx
    push dx
    inc word [dk_rbeat]
    cmp word [dk_rbeat], DK_RINGN
    jb .note
    mov byte [dk_ring], 2
    jmp short .x
.note:
    mov ax, 880
    test byte [dk_rbeat], 1
    jz .n
    mov ax, 660
.n:
    mov cx, 3
    mov dl, 40h
    call OSAPI_SND_TONE
.x:
    pop dx
    pop cx
    pop ax
.r:
    ret

; dk_ringtoast - `Alarm: <label>` on the bar
dk_ringtoast:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    cld
    mov di, dk_line
    mov si, dks_almc
    call dk_cat
    mov si, DKP + DKP_LABEL
    call dk_cat
    xor al, al
    stosb
    mov si, dk_line
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret
dks_almc:   db 'Alarm: ', 0

; dk_snooze - Snooze: quiet, and back in DK_SNOOZE minutes. Keeps everything
dk_snooze:
    cmp byte [dk_ring], 0
    je dk_quiet.r
    push ax
    mov al, [dk_mm]
    add al, DK_SNOOZE
    mov ah, [dk_hh]
    cmp al, 60
    jb .h
    sub al, 60
    inc ah
    cmp ah, 24
    jb .h
    xor ah, ah
.h:
    xchg al, ah
    mov [dk_snh], ax            ; (dk_snm follows it)
    mov al, 1                   ; snoozed
    jmp short dk_quiet

; dk_dismiss - Dismiss: quiet until tomorrow's. Keeps everything
dk_dismiss:
    cmp byte [dk_ring], 0
    jne .go
    cmp byte [dk_snz], 0
    je dk_quiet.r
.go:
    push ax
    xor al, al                  ; not snoozed
; dk_quiet - AL = snoozed or not, AX pushed: the ringing stopped, the tone
; off now, the card's rows and the menus again
dk_quiet:
    mov [dk_snz], al
    mov byte [dk_ring], 0
    push cx
    push dx
    xor ax, ax
    xor cx, cx
    mov dl, 40h
    call OSAPI_SND_TONE
    pop dx
    pop cx
    call dk_relay
    call dk_menulive
    pop ax
.r:
    ret

; dk_relay - the payload or the ringing changed the card's shape: the clock
; laid out again and redrawn (os88stik.inc's stik_resize: the same frame is
; one redraw, another is a hide, a re-size and a show), the hands' record
; taken again. The lock held. Keeps everything
dk_relay:
    push si
    mov byte [dk_cok], 0        ; the face cache is the old face's
    mov si, [stik_win]
    or si, si
    jz .x
    test byte [stik_fl], STIK_F_QUIT
    jnz .x
    call stik_resize
    call dk_hands
    call dk_hkeep
.x:
    pop si
    ret

; dk_onclose - STIK_ONCLOSE: an unapplied preview goes back before the save,
; and the settings and alarm windows go with the clock
dk_onclose:
    push bx
    mov bx, [dk_swin]
    or bx, bx
    jz .a
    call dk_srestore
    mov word [dk_swin], 0
    call OSAPI_WM_DESTROY
.a:
    mov bx, [dk_awin]
    or bx, bx
    jz .x
    mov word [dk_awin], 0
    call OSAPI_WM_DESTROY
.x:
    pop bx
    ret

; -----------------------------------------------------------------------------
; dk_keys - AL = a key's ASCII, AH = its scan code, SI = a KEY TABLE: the
; first entry the key matches runs, AL = its byte, and CF = 0 out; CF = 1
; none, AX as it came. An entry (DKKEY): db the key - ASCII, a letter in
; lower case matching either case, or 80h + a scan code - db its byte, dw its
; proc; 0 ends the table. Keeps BX, CX, SI
; -----------------------------------------------------------------------------
%macro DKKEY 3
    db %1, %2
    dw %3
%endmacro
dk_keys:
    push bx
    push cx
    push si
    mov bl, al                  ; BL = the ASCII, a letter lower case...
    cmp bl, 'A'
    jb .s
    cmp bl, 'Z'
    ja .s
    or bl, 20h
.s:
    or bl, bl
    jns .h
    mov bl, 0FFh                ; ...past 7Fh none of the table's
.h:
    mov bh, ah                  ; BH = the scan code's entry
    or bh, 80h
.e:
    mov cl, [si]
    or cl, cl
    stc
    jz .x
    add si, 4
    cmp cl, bl
    je .hit
    cmp cl, bh
    jne .e
.hit:
    mov al, [si-3]
    call [si-2]
    clc
.x:
    pop si
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; dk_onkey - W_ONKEY, while the clock owns the bar (AL = ASCII, AH = the
; scancode, SI = the window; lock held). Every action has a key:
;   Enter  Settings        A  Alarm...          F  Clock face only
;   S      Seconds         H  24-hour           Left / Right  the face
;   Up / Down  the size    Z  Snooze            D  Dismiss
;   Esc    Dismiss a ringing alarm, else hand the bar back
; -----------------------------------------------------------------------------
dk_onkey:
    push ax
    push bx
    push si
    mov si, dk_kclock
    call dk_keys
    pop si
    pop bx
    pop ax
    ret
dk_kclock:  DKKEY 80h + KSC_LEFT, -1, dk_kface
            DKKEY 80h + KSC_RIGHT, 1, dk_kface
            DKKEY 80h + KSC_UP, 1, dk_ksize
            DKKEY 80h + KSC_DOWN, -1, dk_ksize
            DKKEY 13, 0, dk_setopen
            DKKEY 27, 0, dk_kesc
            DKKEY 'a', 0, dk_almopen
            DKKEY 'f', DKF_ONLY, dk_toggle
            DKKEY 's', DKF_SECS, dk_toggle
            DKKEY 'h', DKF_H24, dk_toggle
            DKKEY 'z', 0, dk_snooze
            DKKEY 'd', 0, dk_dismiss
            db 0

; dk_kface - AL = -1 / +1: the face before or after, round the nine
dk_kface:
    call dk_fstep
    jmp short dk_setface

; dk_fstep - AL = a step, -8..8: AL = the face that far from the payload's,
; round the nine (the clock's keys, and the settings' gallery's)
dk_fstep:
    add al, [DKP + DKP_FACE]
    jns .p
    add al, DKF_N
.p:
    cmp al, DKF_N
    jb .r
    sub al, DKF_N
.r:
    ret

; dk_ksize - AL = -1 / +1: the size smaller or larger, and no further
dk_ksize:
    add al, [DKP + DKP_SIZE]
    cmp al, 3
    jb dk_setsize
    ret

; dk_kesc - Esc: Dismiss a ringing alarm, else RELEASE - the bar to the
; front application. BX spent
dk_kesc:
    cmp byte [dk_ring], 0
    jne dk_dismiss
    mov bx, [dk_win]
    mov al, 3
    call OSAPI_WM_DESK
    ret

; dk_setface / dk_setsize / dk_toggle - one setting changed from the clock
; itself (its keys, its menus): it is COMMITTED - saved, and, with the
; settings window up, its preview and its applied copy follow. Keep all
dk_setface:
    cmp al, [DKP + DKP_FACE]
    je .r
    mov [DKP + DKP_FACE], al
    jmp dk_commit
.r:
    ret
dk_setsize:
    cmp al, [DKP + DKP_SIZE]
    je .r
    mov [DKP + DKP_SIZE], al
    jmp dk_commit
.r:
    ret
dk_toggle:
    xor [DKP + DKP_FLAGS], al
dk_commit:
    call dk_relay
    call dk_menulive
    call stik_touch
    cmp word [dk_swin], 0
    je .r
    call dk_sapplied            ; the window's copy is this too
    call dk_sredraw
.r:
    ret

; -----------------------------------------------------------------------------
; the menus (SPEC.md 12.2): Clock, Face and Size - every setting the window
; has, from the bar
; -----------------------------------------------------------------------------
dk_oncmd:
    push ax
    push bx
    cmp ah, 1
    je .face
    cmp ah, 2
    je .size
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [dk_cmds + bx]
    jmp short .x
.face:
    call dk_setface
    jmp short .x
.size:
    call dk_setsize
.x:
    pop bx
    pop ax
    ret
dk_cmds:    dw dk_setopen, dk_almopen, dk_c_only, dk_c_secs, dk_c_h24
            dw dk_snooze, dk_dismiss
dk_c_only:
    mov al, DKF_ONLY
    jmp dk_toggle
dk_c_secs:
    mov al, DKF_SECS
    jmp dk_toggle
dk_c_h24:
    mov al, DKF_H24
    jmp dk_toggle

; dk_about - the app-name cell's About: a toast, the clock being too small a
; card to hold os88ui's (SPEC.md 12.2)
dk_about:
    push cx
    push si
    push es
    push ds
    pop es
    mov si, dks_about
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop si
    pop cx
    ret
dks_about:  db 'Desk Clock - 9 faces', 0

; dk_menulive - the items as the state stands: the toggles ticked, the face
; and the size marked, Snooze and Dismiss greyed when nothing rings (SPEC.md
; 47). An item is stored ONCE with a two-cell gutter, and its mark is the
; gutter's first byte - `*` or a space - written here (Sticky Note's Color
; menu marks the same way). Keeps everything
dk_menulive:
    push ax
    push bx
    push cx
    push si
    mov si, dk_i_clock + 2 * DKM_ONLY
    mov ah, DKF_ONLY            ; the three toggles: ONLY, SECS, H24
.tg:
    mov al, [DKP + DKP_FLAGS]
    and al, ah
    cmp al, ah
    call .mk
    shl ah, 1
    cmp ah, DKF_ALARM
    jb .tg
    mov bx, dk_i_snz            ; ringing: Snooze; snoozed or ringing:
    cmp byte [dk_ring], 0       ; Dismiss
    jne .s
    mov bx, dk_i_snz0
.s:
    mov [si], bx
    mov bx, dk_i_dis
    cmp byte [dk_ring], 0
    jne .d
    cmp byte [dk_snz], 0
    jne .d
    mov bx, dk_i_dis0
.d:
    mov [si+2], bx
    mov si, dk_i_face           ; the face marked, then the size
    mov ah, [DKP + DKP_FACE]
    mov cx, DKF_N
    call .mark
    mov ah, [DKP + DKP_SIZE]
    mov cl, 3
    call .mark
    pop si
    pop cx
    pop bx
    pop ax
    ret
; SI = the menu's slots, AH = the marked one, CX = n
.mark:
    xor al, al
.m:
    cmp al, ah
    call .mk
    inc al
    loop .m
    ret
; ZF = 1 marks the item at [SI]; SI to the next slot
.mk:
    mov bx, [si]
    mov byte [bx], ' '
    jne .k
    mov byte [bx], '*'
.k:
    inc si
    inc si
    ret

; --- the clock's window (SPEC.md 110.11, 110.14) ------------------------------
dk_tpl:
    dw 23, 60, 130, 200         ; stik_load sizes and places it (STIK_RIGHT)
    dw dk_title, dk_paint, dk_onkey, 0

dk_paint:
    jmp stik_paint

dk_title:   db 'Desk Clock', 0

    OS88_MENUSET dk_menus, dk_title, dk_oncmd   ; AM_NAME NOT 0 (SPEC.md 110.9)
        OS88_MENU dk_m_clock, dk_i_clock, DKM_N
        OS88_MENU dk_m_face, dk_i_face, DKF_N
        OS88_MENU dk_m_size, dk_i_size, 3
    OS88_MENUSET_END dk_menus
DKM_SET     equ 0               ; the Clock menu's items, in order (dk_cmds)
DKM_ALM     equ 1
DKM_ONLY    equ 2
DKM_SECS    equ 3
DKM_H24     equ 4
DKM_SNZ     equ 5
DKM_DIS     equ 6
DKM_N       equ 7
dk_m_clock: db 'Clock', 0
dk_m_face:  db 'Face', 0
dk_m_size:  db 'Size', 0
dk_i_clock: dw dk_i_set, dk_i_alm, dk_t_only, dk_t_secs, dk_t_h24, 0, 0
dk_i_face:  dw dk_t_f0, dk_t_f1, dk_t_f2, dk_t_f3, dk_t_f4, dk_t_f5
            dw dk_t_f6, dk_t_f7, dk_t_f8
dk_i_size:  dw dk_t_s0, dk_t_s1, dk_t_s2
dk_i_set:   db 'Settings...  Enter', 0
dk_i_alm:   db 'Alarm...  A', 0
dk_i_snz:   db 'Snooze  Z', 0
dk_i_snz0:  db MENU_DIS, 'Snooze (not ringing)', 0
dk_i_dis:   db 'Dismiss  D', 0
dk_i_dis0:  db MENU_DIS, 'Dismiss (not ringing)', 0
%macro DK_ITEM 2+               ; label, text: the mark's gutter, the text -
%1:     db '  ', %2, 0          ; and the face's and the size's NAME is the
%endmacro                       ; text, at the label + 2 (dk_faces, dkss_sizes)
DK_ITEM dk_t_only, 'Clock Face Only  F'
DK_ITEM dk_t_secs, 'Show Seconds  S'
DK_ITEM dk_t_h24,  '24-Hour Clock  H'
DK_ITEM dk_t_f0,   'Classic'
DK_ITEM dk_t_f1,   'Memphis ', 27h, '80'
DK_ITEM dk_t_f2,   'Neon ', 27h, '90'
DK_ITEM dk_t_f3,   'Mono Digital'
DK_ITEM dk_t_f4,   'Casio LCD'
DK_ITEM dk_t_f5,   'Casio VGA'
DK_ITEM dk_t_f6,   'Red LED'
DK_ITEM dk_t_f7,   'Amber LED'
DK_ITEM dk_t_f8,   'Flip Clock'
DK_ITEM dk_t_s0,   'Small'
DK_ITEM dk_t_s1,   'Medium'
DK_ITEM dk_t_s2,   'Large'

; DK_PARITY p - 0 or 1 byte, so the next byte's offset has parity p: the hot
; code's (the composer, the faces, the clock's paint and its hands) stays at
; the parity it was measured at whatever the cold code around it does - an
; odd shift re-prices every word fetch on a 16-bit bus (tools/os88rdiff.py)
%macro DK_PARITY 1
    times (($ - $$) + %1) & 1 db 0
%endmacro

    DK_PARITY 1                 ; dk_lc's, measured (SPEC.md 110.14.4)
%include "dkdraw.inc"
%include "dkface.inc"

%define OS88UI_MACROS           ; no os88ui control: the forms draw their
                                ; own (dkset.inc) and About is a toast
%include "os88ui.inc"
%include "os88gfx.inc"
%ifdef STIK_DESKONLY            ; the include's paint path (stik_paint ..
    DK_PARITY 0                 ; stik_desk) at its measured parity: what
%else                           ; STIK_DESKONLY takes out ahead of it is
    DK_PARITY 1                 ; an odd number of bytes
%endif
%include "os88stik.inc"
%include "dkwid.inc"            ; AFTER os88stik.inc: its STIKL_* and macros
%include "dkset.inc"
%include "dkalm.inc"

%if STIKL_SZ != 12
  %error "deskclok: dk_lay is one STIK_LAYOUT entry"
%endif

    align 16, db 0              ; os88stik.inc's read lands on a paragraph
    OS88_BSS STIK_BSS_END
    OS88_IMAGE_END
