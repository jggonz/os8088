; =============================================================================
; os8088 - apps/stiknote/stiknote.asm
;
; STICKY NOTE - the second STICKY APP (SPEC.md 110, its own section 110.13).
; Notes and checklists on the desktop, kept where they were put and brought
; back at the next boot: it ships in STIKAPPS\ on the system disk and includes
; apps/os88stik.inc, which owns its place, its state file
; (SYSTEM\APPDATA\STIKNOTE.DAT) and its row in the desktop configuration.
;
; TWO WINDOWS, ONE PROGRAM:
;
;   THE PAD      the Sticky App's own window - a DESK window (SPEC.md 11.103)
;                wearing os88stik.inc's widget treatment: ONE card, a sheet on
;                a stack of sheets with its corner turned, showing the
;                current note: a pin, its title, `+`, the note's lines and
;                boxes, and `< 1 / 3 >` with the open-in-window arrow. It is
;                the window the instance is bound to, so the boot relaunch,
;                the Sticky Apps menu and the close all mean it.
;   THE EDITOR   an ordinary titled window opened from the pad (the arrow,
;                `+`, or the Note menu): New, Delete and `On desktop` along
;                the top, the notes listed on the left, the note being edited
;                on the right with its scroll bar, and the count and `Saved`
;                along the bottom. It is a package's SECOND window (SPEC.md
;                42.1): unbound, so no dock tile, and its close box is ours to
;                answer (OSAPI_WM_ONCLOSE) - it hides, or, with the pad off
;                the desktop, closes the program.
;
; A NOTE is plain text in one shared 4 KB pool (up to 24 notes): its first
; line is its TITLE, and a body line that begins with a MARKER byte is a
; check box - SN_BOX0 open, SN_BOX1 ticked. Typing `[ ]` or `[]` at the start
; of a line makes one, Ctrl+T toggles one, a click ticks one - in the editor
; and on the pad alike. Everything else is word-wrapped text.
;
; COLOUR: every note has one of five papers - yellow, pink, green, blue,
; white. A colour adapter draws the pad's sheet and the editor's page in it
; (OSAPI_GFX_BLIT1_PEN: the composed band's lit bits become the paper); a
; 1bpp adapter has nowhere to put a colour and draws them white, and the
; Color menu greys itself there with the reason (SPEC.md 47).
;
;   stiknote.asm  this: header, payload, bss, entry, menus, the tick
;   sntext.inc    the note model: the pool, the wrap walk, inserts, deletes
;   snpad.inc     the pad: its three layouts, its composer, its presses
;   sned.inc      the editor window: layout, composer, keys, clicks
;
; Every pixel of both windows is COMPOSED into one 1bpp band and put down a
; slab at a time with OSAPI_GFX_BLIT1 (SPEC.md 110.11, os88gfx.inc) - no
; fill-then-letter, nothing written twice.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'Sticky Note', sn_entry, OS88_F_ICON | OS88_F_DESK | OS88_F_STIK

; A note with its corner turned: also the Sticky App row's picture
; (STIK_ICON), so a row the boot could not launch becomes a desktop icon that
; looks like what it was (SPEC.md 110.4).
    OS88_ICON16
    dw 0x0000, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE
    dw 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFE, 0x7FFC, 0x7FF8, 0x7FF0, 0x0000
    dw 0x0000, 0x7FFE, 0x4002, 0x47E2, 0x4002, 0x5FFA, 0x4002, 0x4FC2
    dw 0x4002, 0x4F82, 0x4002, 0x401E, 0x4014, 0x4018, 0x7FF0, 0x0000
    OS88_ICON16_END

; The Sticky Apps menu's row for us (SPEC.md 110.12): a pinned sheet on a
; stack, a title, a rule, three boxes - one ticked - and the turned corner.
    OS88_STIKCARD 'Sticky Note', 'Notes & checklists'
    OS88_STIKROW '................................'
    OS88_STIKROW '................................'
    OS88_STIKROW '...########################.....'
    OS88_STIKROW '...#......................#.....'
    OS88_STIKROW '...#.###..................###...'
    OS88_STIKROW '...#..#...#######......#..#.#...'
    OS88_STIKROW '...#.###..............###.#.#...'
    OS88_STIKROW '...#..#................#..#.#...'
    OS88_STIKROW '...#......................#.#...'
    OS88_STIKROW '...#.####################.#.#...'
    OS88_STIKROW '...#......................#.#...'
    OS88_STIKROW '...#.###.#########........#.#...'
    OS88_STIKROW '...#.#.#..................#.#...'
    OS88_STIKROW '...#.###..................#.#...'
    OS88_STIKROW '...#......................#.#...'
    OS88_STIKROW '...#.###.###########......#.#...'
    OS88_STIKROW '...#.#.#..................#.#...'
    OS88_STIKROW '...#.###..................#.#...'
    OS88_STIKROW '...#......................#.#...'
    OS88_STIKROW '...#.###.#######..........#.#...'
    OS88_STIKROW '...#.###..................#.#...'
    OS88_STIKROW '...#.###..................#.#...'
    OS88_STIKROW '...#......................#.#...'
    OS88_STIKROW '...#.############.....#####.#...'
    OS88_STIKROW '...#..................#..#..#...'
    OS88_STIKROW '...#..................#.#...#...'
    OS88_STIKROW '...#..................##....#...'
    OS88_STIKROW '...####################.....#...'
    OS88_STIKROW '.....#......................#...'
    OS88_STIKROW '.....########################...'
    OS88_STIKROW '................................'
    OS88_STIKROW '................................'
    OS88_STIKCARD_END

; --- the payload v1 (SPEC.md 110.13) ------------------------------------------
; tools/os88stik.py's STIKNOTE_* are the same numbers; t_stik holds them equal
SN_MAXN     equ 24              ; notes
SN_POOL     equ 4096            ; ...sharing one pool of text
SNP_N       equ 0               ; db notes in use
SNP_CUR     equ 1               ; db the current one: the pad's page and the
                                ; editor's selection are one note
SNP_FLAGS   equ 2               ; db SNF_*
SNF_DESK    equ 1               ; the pad is on the desktop
SNF_INIT    equ 80h             ; the welcome note was made once
SNP_COL     equ 4               ; 24 db: each note's paper, 0..SN_NCOL-1
SNP_LENS    equ 28              ; 24 dw: each note's length, in pool order
SNP_POOL    equ 80              ; 4096: the text, note after note
SNP_LEN     equ SNP_POOL + SN_POOL      ; 4176
SNP_VER     equ 1
%if SNP_COL + SN_MAXN > SNP_LENS || SNP_LENS + 2 * SN_MAXN > SNP_POOL
  %error "stiknote: the payload's tables overlap"
%endif
SN_BOX0     equ 1               ; a body line's first byte: an open box...
SN_BOX1     equ 2               ; ...or a ticked one
SN_NCOL     equ 5               ; the papers (sn_pens)

KSC_HOME    equ 0x47
KSC_END     equ 0x4F
KSC_PGUP    equ 0x49
KSC_PGDN    equ 0x51
KSC_DEL     equ 0x53
SN_C_BOX    equ 0x14            ; Ctrl+T
SN_C_NEW    equ 0x0E            ; Ctrl+N

; --- the Sticky App (apps/os88stik.inc, SPEC.md 110.5, 110.11) ---------------
%define STIK_DATNAME 'STIKNOTE.DAT'
%define STIK_CAP     'Sticky Note'
%define STIK_ISOPEN  'Sticky Note is open'
%define STIK_DAMAGEDQ 'Replace damaged notes?'
%define STIK_ONTICK  sn_tick
STIK_PAYLEN equ SNP_LEN
STIK_PAYVER equ SNP_VER
STIK_ICON   equ 32
STIK_BSS    equ SN_BSS          ; the include's state follows ours
%define STIK_WIDGET
%define STIK_COLOR              ; the titled fallback's save-under keeps four
                                ; planes: a pink sheet is not two colours
%define STIK_TAG      'Sticky Note'     ; = the title
%define STIK_FACE_W 192        ; 24 cells: the sheet and the stack behind it
                                ; (a %define: the include tests %ifndef)
%define STIK_RIGHT    472       ; left of the Desk Calendar's 264
%define STIK_LAYOUTS  snp_lays
%define STIK_PAINTCARD snp_pcard
%define STIK_ONCARD   snp_oncard
%define STIK_ONCARDUP snp_oncardup
%define GFXE_TEXT
%define GFXE_BITS
%define GFXE_BAND_W   480       ; the editor's widest content (sne_onsize)
%define GFXE_BAND_H   32
%define GFXE_BAND_BUF sn_band

; --- bss: OURS FIRST, at fixed offsets ----------------------------------------
%assign SNB 0
%macro SNV 2                    ; name, bytes
%1 equ os88_image_end + SNB
%assign SNB SNB + %2
%endmacro
    SNV sne_win, 2              ; the editor window, 0: never made
    SNV sn_abwin, 2             ; the window the About card is up on, 0 none
    SNV sne_open, 1             ; the editor is up (as far as we know)
    SNV sn_tq, 1                ; an edit since the last poll
    SNV sn_ask, 1               ; our Delete question is up
    SNV sn_mono, 1              ; the primary is 1bpp: no colours
    SNV sne_car, 2              ; the caret: an offset into the current note
    SNV sne_top, 2              ; the editor's first body line on show
    SNV sne_goal, 1             ; the column Up and Down aim for
    SNV sne_ltop, 1             ; the list's first note on show
    SNV sne_jb, 1               ; a box was just made: one space is swallowed
    SNV sne_stat, 1             ; the status word on the glass, SNS_*
    SNV sne_cw, 2               ; the editor's content size and origin, as
    SNV sne_ch, 2               ; ...the last paint, click or draw found them
    SNV sne_ox, 2
    SNV sne_oy, 2
    SNV sne_cx0, 2              ; the composer's x window, content px
    SNV sne_cx1, 2
    SNV sne_r0, 2               ; sne_rect's rows and columns
    SNV sne_r1, 2
    SNV sne_x0, 2
    SNV sne_x1, 2
    SNV sne_ba, 2               ; ...the slab being composed: its first row
    SNV sne_bn, 2               ; ...and its rows
    SNV sne_press, 1            ; the element a press went to, SNE_H_*
    SNV sne_lit, 1              ; ...a button drawn down
    SNV sne_l0, 2               ; an edit's first changed line (sne_after)
    SNV sne_nt0, 2              ; ...and the body's lines before it
    SNV sne_tk, 1               ; ...0 body, 1 title, 2 title and body
    SNV sne_sbx, 2              ; the scroll bar's x
    SNV sne_pb, 2               ; the status's first row
    SNV sne_nv, 2               ; the body lines that fit
    SNV sne_nl, 1               ; the list rows that fit
    SNV sne_tw, 1               ; the page's cells: a text line's
    SNV sne_bw, 1               ; ...a box line's
    SNV sne_vert, 1             ; the last move was Up or Down: keep the goal
    SNV snp_dirty, 1            ; the pad is owed a redraw at the next beat
    SNV snp_press, 1            ; the pad element a press went to, SNP_H_*
    SNV snp_r0, 2               ; snp_pcard: the damaged rows...
    SNV snp_r1, 2
    SNV snp_ba, 2               ; ...the slab's first row and its rows
    SNV snp_bn, 2
    SNV sn_boot, 1              ; the first beat owes the pad its hide
    SNV it_p, 2                 ; THE WALK (sntext.inc): this line's start
    SNV it_e, 2                 ; ...the note's end
    SNV it_le, 2                ; ...this line's content end
    SNV it_n, 2                 ; ...the next line's start
    SNV it_k, 1                 ; ...the paragraph's kind: 0, SN_BOX0/1
    SNV it_f, 1                 ; ...1: the paragraph's first line
    SNV it_lf, 1                ; ...it ended at a LF
    SNV it_end, 1               ; ...it ended at the note's end
    SNV it_w, 1                 ; ...the widths: a text line's, in cells
    SNV it_wb, 1                ; ...and a box line's
    SNV sn_delnote, 1           ; the note our Delete question is about
    SNV sn_t0, 2                ; scratch words
    SNV sn_t1, 2
    SNV sn_t2, 2
    SNV sn_t3, 2
    SNV sn_line, 48             ; a string being built
%assign SNB (SNB + 15) & ~15
SN_BSS_BAND equ SNB
    SNV sn_band, GFXE_BAND_W / 8 * GFXE_BAND_H
SN_BSS      equ SNB             ; a multiple of 16
%if SN_BSS % 16
  %error "stiknote: its own bss must end on a paragraph"
%endif

SNS_SAVED   equ 0               ; sne_stat
SNS_EDITED  equ 1
SNS_NOSAVE  equ 2

; -----------------------------------------------------------------------------
; sn_entry - the entry proc (DS = CS = ours, ES = KERNEL_SEG, the UI task)
; in:  SI = the loader's name, for os88stik.inc
; out: CF = 0 launched; CF = 1 not (another Sticky Note is up, or no window)
; -----------------------------------------------------------------------------
sn_entry:
    push si
    push di
    mov di, snp_tpl
    call stik_load              ; QUERY, the layout by the screen's height,
    jc .out                     ; the template's size and place (110.11)
    call OSAPI_VIDEO
    mov al, 0
    cmp dh, 1
    ja .col
    inc ax
.col:
    mov [sn_mono], al
    call sn_check               ; a payload that is not ours is DAMAGED:
    jnc .ok                     ; empty, and the first save asks first
    push es
    push ds
    pop es
    mov di, STIK_PAY
    mov cx, SNP_LEN
    xor al, al
    cld
    rep stosb
    pop es
    or byte [stik_st], STIK_S_DAMAGED
    mov byte [STIK_PAY + SNP_FLAGS], SNF_DESK   ; empty, and on the desk
    jmp short .seeded
.ok:
    call sn_ztail               ; a file an older build wrote may carry a
                                ; deleted note's bytes past the pool's end
    test byte [STIK_PAY + SNP_FLAGS], SNF_INIT
    jnz .seeded
    test byte [stik_st], STIK_S_RDONLY
    jz .seed                    ; UNREAD (an I/O error): nothing is known, so
    mov byte [STIK_PAY + SNP_FLAGS], SNF_DESK   ; no welcome - but the pad
    jmp short .seeded           ; is on the desk
.seed:
    call sn_seed                ; the first launch: the welcome note (with no
                                ; SYSTEM\APPDATA it is never saved)
.seeded:
    mov si, snp_tpl
    call OSAPI_WM_CREATE        ; BX = the pad, CF = the table is full
    jc .out
    OS88_REGION_MOVABLE sn_ret
    mov al, 1
    call OSAPI_WM_KEEPH         ; a fixed layout: hang over the dock, never cut
    mov si, sn_menus
    call OSAPI_MENU_SET
    mov si, sn_about
    call OSAPI_ABOUT_SET
    call stik_made              ; MAKE, the prefilters, the tick
    test byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    jnz .live
    call sne_show               ; off the desktop: the editor is what opens,
    cmp word [sne_win], 0       ; and the loader shows the pad after we
    je .live                    ; return, so the first beat hides it - unless
    mov byte [sn_boot], 1       ; there is no editor to be had
.live:
    call sn_menulive
    clc
.out:
    pop di
    pop si
sn_ret:
    ret

; -----------------------------------------------------------------------------
; sn_check - CF = 1 the payload is not one we wrote: the counts, the current
; note, the papers, the lengths and their sum. Every register kept
; -----------------------------------------------------------------------------
sn_check:
    push ax
    push bx
    push cx
    push dx
    mov dl, [STIK_PAY + SNP_N]
    cmp dl, SN_MAXN
    ja .bad
    mov al, [STIK_PAY + SNP_CUR]
    or dl, dl
    jz .c0
    cmp al, dl
    jae .bad
    jmp short .lens
.c0:
    or al, al
    jnz .bad
.lens:
    xor ax, ax                  ; the sum
    xor cx, cx                  ; CL = the note
    mov bx, STIK_PAY + SNP_LENS
.l:
    cmp cl, dl
    jb .used
    cmp word [bx], 0            ; past the count: nothing
    jne .bad
    jmp short .n
.used:
    push bx
    mov bl, cl
    xor bh, bh
    cmp byte [STIK_PAY + SNP_COL + bx], SN_NCOL
    pop bx
    jae .bad
    add ax, [bx]
    jc .bad
.n:
    add bx, 2
    inc cx
    cmp cl, SN_MAXN
    jb .l
    cmp ax, SN_POOL
    ja .bad
    clc
    jmp short .x
.bad:
    stc
.x:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; sn_seed - the welcome note, once (SNF_INIT), on the pad
sn_seed:
    push ax
    push cx
    push si
    push di
    push es
    push ds
    pop es
    mov si, sn_welcome
    mov di, STIK_PAY + SNP_POOL
    mov cx, SN_WELLEN
    cld
    rep movsb
    mov byte [STIK_PAY + SNP_N], 1
    mov byte [STIK_PAY + SNP_CUR], 0
    mov byte [STIK_PAY + SNP_COL], 0
    mov word [STIK_PAY + SNP_LENS], SN_WELLEN
    mov byte [STIK_PAY + SNP_FLAGS], SNF_DESK | SNF_INIT
    call stik_touch             ; the first beat writes it
    pop es
    pop di
    pop si
    pop cx
    pop ax
    ret

sn_welcome:
    db 'Welcome', 10
    db 'Notes on your desk.', 10
    db SN_BOX0, 'Type [ ] for a box', 10
    db SN_BOX0, 'Ctrl+T toggles one', 10
    db SN_BOX1, 'Click to tick it', 10
    db 10
    db 'Edit with the arrow.'
SN_WELLEN   equ $ - sn_welcome

; -----------------------------------------------------------------------------
; sn_tick - STIK_ONTICK, every beat (the UI task, lock held, no clip): the pad
; owed a redraw gets it, the editor's status word follows the save, and the
; two windows' visibility is reconciled - a minimized editor (its own box is
; a hide: it has no dock tile) brings the pad back, and a pad Show All
; brought back is on the desktop again.
; out: CF = 1 HOLD the save owed: an edit landed since the last poll, or our
;      Delete question is up - a burst of typing is one save
; -----------------------------------------------------------------------------
sn_tick:
    push ax
    push bx
    push si
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov si, [stik_win]
    cmp byte [sn_boot], 0
    je .norm
    mov byte [sn_boot], 0       ; launched off the desktop: the pad the loader
    mov bx, si                  ; showed goes (sn_entry)
    call OSAPI_WM_HIDE
    jmp short .ed
.norm:
    cmp byte [sne_open], 0      ; the editor still up? A minimize hides an
    je .pv                      ; unbound window, and nothing tells us
    mov bx, [sne_win]
    test byte [es:bx + W_FLAGS], 2
    jnz .pv
    mov byte [sne_open], 0
    call sn_menulive
    test byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    jnz .pv
    or byte [STIK_PAY + SNP_FLAGS], SNF_DESK ; it was the only window: the
    mov bx, si                  ; pad comes back
    call OSAPI_WM_SHOW
    call sn_dtouch
    jmp short .ed
.pv:
    test byte [es:si + W_FLAGS], 2
    jz .ed
    test byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    jnz .ed
    or byte [STIK_PAY + SNP_FLAGS], SNF_DESK ; shown by somebody else (the
    call sn_dtouch              ; Sticky Apps menu's Show All): on the desk
.ed:
    cmp byte [snp_dirty], 0
    je .st
    call snp_redraw             ; the pad, once a burst of typing is over
.st:
    call sne_status             ; `Saved` once the save has landed
    mov al, [sn_tq]             ; an edit this poll, or the question up:
    mov byte [sn_tq], 0         ; CF = 1 holds the save
    or al, [sn_ask]
    neg al
    pop es
    pop si
    pop bx
    pop ax
    ret

; sn_dtouch - the desk flag changed: owed a save, the menus and the box
sn_dtouch:
    call stik_touch
    call sn_menulive
    jmp sne_rbar

; sn_edit - a note's text or a note's place changed: owed a save, the pad a
; redraw at the next beat, and the status word now. Every register kept
sn_edit:
    call stik_touch
    mov byte [sn_tq], 1
    mov byte [snp_dirty], 1
    jmp sne_status

; sn_setdesk - AL = 1 the pad on the desktop, 0 off it (the box, the menu).
; Off it with no editor up opens the editor: a program with no window on the
; glass would be one nobody could reach
sn_setdesk:
    push ax
    push bx
    mov ah, [STIK_PAY + SNP_FLAGS]
    and ah, SNF_DESK
    cmp al, ah
    je .x
    mov bx, [stik_win]
    or al, al
    jz .off
    or byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    call OSAPI_WM_SHOW
    jmp short .t
.off:
    call sne_lay                ; the editor ON THE GLASS - not as far as we
    jnc .h                      ; know: a minimize hid it behind our back
    call sne_show
    call sne_lay
    jc .x                       ; none to be had: the pad stays, and the flag
.h:
    and byte [STIK_PAY + SNP_FLAGS], ~SNF_DESK & 0FFh
    call OSAPI_WM_HIDE
.t:
    call sn_dtouch
.x:
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the menus - one set on both windows (SPEC.md 12.2): AL = the item, AH = the
; menu, SI = the window it was picked on; lock held
; -----------------------------------------------------------------------------
sn_oncmd:
    push ax
    push bx
    push si
    call sn_abdismiss
    cmp ah, 1
    je .col
    xor ah, ah
    mov bx, ax
    shl bx, 1
    call [sn_cmds + bx]
    jmp short .x
.col:
    call sn_setcol
.x:
    call sn_menulive
    pop si
    pop bx
    pop ax
    ret

sn_cmds:    dw sn_new, sn_askdel, sne_cbox, sn_prev, sn_next, sne_show
            dw sn_cmdesk

; sn_cmdesk - the Note menu's desk item: the flag the other way
sn_cmdesk:
    push ax
    mov al, [STIK_PAY + SNP_FLAGS]
    and al, SNF_DESK
    xor al, SNF_DESK
    call sn_setdesk
    pop ax
    ret

; sn_setcol - the current note's paper AL (0..4): both windows' paper
sn_setcol:
    cmp byte [sn_mono], 0
    jne .r                      ; greyed: a shortcut never comes here either
    cmp byte [STIK_PAY + SNP_N], 0
    je .r
    push bx
    mov bl, [STIK_PAY + SNP_CUR]
    xor bh, bh
    cmp [STIK_PAY + SNP_COL + bx], al
    je .x
    mov [STIK_PAY + SNP_COL + bx], al
    call stik_touch
    call snp_redraw
    call sne_rpane
.x:
    pop bx
.r:
    ret

; sn_new - a new, empty note after the last; it is current, and the editor
; opens on it with the caret in its title
sn_new:
    push ax
    push bx
    mov al, [STIK_PAY + SNP_N]
    cmp al, SN_MAXN
    jb .ok
    mov si, sn_s_full
    call sn_toast
    jmp short .x
.ok:
    mov bl, al
    xor bh, bh
    mov byte [STIK_PAY + SNP_COL + bx], 0  ; yellow
    shl bx, 1
    mov word [STIK_PAY + SNP_LENS + bx], 0
    inc byte [STIK_PAY + SNP_N]
    mov [STIK_PAY + SNP_CUR], al
    call sn_edit
    call sne_newcur             ; the caret, the scroll, the list
    call sne_show               ; open, in front, drawn
    call sne_rall
    call snp_redraw
.x:
    pop bx
    pop ax
    ret

; sn_askdel - Delete: asked first (os88ui_ask), on whichever window is up
sn_askdel:
    cmp byte [STIK_PAY + SNP_N], 0
    je .r
    cmp byte [sn_ask], 0
    jne .r
    push ax
    push bx
    push si
    push di
    push es
    mov ax, KERNEL_SEG
    mov es, ax
    mov bx, [stik_win]
    cmp byte [sne_open], 0
    je .w
    mov bx, [sne_win]
.w:
    mov al, OS88UI_ADANGER
    mov si, sn_s_qdel
    mov di, sn_delans
    call os88ui_ask
    jc .busy
    mov byte [sn_ask], 1
    mov al, [STIK_PAY + SNP_CUR] ; the note ASKED about: the question is not
    mov [sn_delnote], al        ; modal, and the current one may change
.busy:
    pop es
    pop di
    pop si
    pop bx
    pop ax
.r:
    ret

; sn_delans - the question's answer: AL = 1 Proceed (ADANGER's index 1)
sn_delans:
    mov byte [sn_ask], 0
    cmp al, 1
    jne .r
    mov al, [sn_delnote]        ; the note the question named, made current
    cmp al, [STIK_PAY + SNP_N]  ; (notes are only ever ADDED after it while
    jae .r                      ; the question is up: its index still holds)
    mov [STIK_PAY + SNP_CUR], al
    call sn_delete
    call sn_menulive
.r:
    ret

; sn_delete - the current note out of the pool and the tables
sn_delete:
    push ax
    push bx
    push cx
    push si
    push di
    push es
    push ds
    pop es
    call sn_cur                 ; SI = it, CX = its length
    push cx
    mov bx, 0
    call sn_del                 ; the text (its length goes to 0)
    pop cx
    mov al, [STIK_PAY + SNP_CUR]
    xor ah, ah
    mov cl, [STIK_PAY + SNP_N]
    xor ch, ch
    sub cx, ax
    dec cx                      ; CX = the notes after it
    mov di, STIK_PAY + SNP_COL
    add di, ax
    mov si, di
    inc si
    push cx
    cld
    rep movsb                   ; the papers down one
    mov byte [di], 0
    pop cx
    mov di, ax
    shl di, 1
    add di, STIK_PAY + SNP_LENS
    mov si, di
    add si, 2
    rep movsw                   ; the lengths down one
    mov word [di], 0
    dec byte [STIK_PAY + SNP_N]
    mov al, [STIK_PAY + SNP_N]
    or al, al
    jz .cur
    cmp [STIK_PAY + SNP_CUR], al
    jb .cur
    dec al
    mov [STIK_PAY + SNP_CUR], al
.cur:
    cmp byte [STIK_PAY + SNP_N], 0
    jne .k
    mov byte [STIK_PAY + SNP_CUR], 0
.k:
    call sn_edit
    call sne_newcur
    call sne_rall
    call snp_redraw
    pop es
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret

; sn_prev / sn_next / sn_goto - another note current (AL = it for sn_goto)
sn_prev:
    push ax
    mov al, [STIK_PAY + SNP_CUR]
    or al, al
    jz .x
    dec ax
    call sn_goto
.x:
    pop ax
    ret

sn_next:
    push ax
    mov al, [STIK_PAY + SNP_CUR]
    inc ax
    cmp al, [STIK_PAY + SNP_N]
    jae .x
    call sn_goto
.x:
    pop ax
    ret

sn_goto:
    cmp al, [STIK_PAY + SNP_N]
    jae .r
    cmp al, [STIK_PAY + SNP_CUR]
    je .r
    push ax
    push bx
    mov ah, [STIK_PAY + SNP_CUR]
    mov [STIK_PAY + SNP_CUR], al
    call stik_touch             ; the page is remembered - not an edit
    mov bl, [sne_ltop]          ; the list's scroll BEFORE newcur moves it
    call sne_newcur
    call sne_rswitch            ; AH = the old note, BL the old scroll: its
    call snp_redraw             ; row, the new one's, the page, the bar
    call sn_menulive
    pop bx
    pop ax
.r:
    ret

; sn_toast - SI = a line (DS)
sn_toast:
    push cx
    push es
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    pop es
    pop cx
    ret

; sn_pen - AL = the paper of the CURRENT note (CWHITE with no notes)
sn_pencur:
    push bx
    mov al, CWHITE
    cmp byte [STIK_PAY + SNP_N], 0
    je .x
    mov bl, [STIK_PAY + SNP_CUR]
    xor bh, bh
    mov bl, [STIK_PAY + SNP_COL + bx]
    mov al, [sn_pens + bx]
.x:
    pop bx
    ret

; sn_setpen - AL = the paper for the next BLIT1s (ink black): OSAPI_GFX_BLIT1_PEN,
; which a 1bpp adapter does not read and the lock's release puts back to
; CWHITE. Every register kept
sn_setpen:
    push ax
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
    pop ax
    ret

sn_pens:    db CYELLOW, CLMAGENTA, CLGREEN, CLCYAN, CWHITE

; -----------------------------------------------------------------------------
; sn_menulive - the items greyed and labelled as the state stands (SPEC.md 47)
; -----------------------------------------------------------------------------
sn_menulive:
    push ax
    push bx
    push cx
    push si
    mov al, [STIK_PAY + SNP_N]
    mov bx, sn_i_del
    or al, al
    jnz .d
    mov bx, sn_i_del0
.d:
    mov [sn_i_note + 2 * SNM_DEL], bx
    mov bx, sn_i_box
    or al, al
    jz .b0
    cmp byte [sne_open], 0
    jne .b
.b0:
    mov bx, sn_i_box0
.b:
    mov [sn_i_note + 2 * SNM_BOX], bx
    mov bx, sn_i_prev
    cmp byte [STIK_PAY + SNP_CUR], 0
    jne .p
    mov bx, sn_i_prev0
.p:
    mov [sn_i_note + 2 * SNM_PREV], bx
    mov bx, sn_i_next
    mov ah, [STIK_PAY + SNP_CUR]
    inc ah
    cmp ah, al
    jb .n
    mov bx, sn_i_next0
.n:
    mov [sn_i_note + 2 * SNM_NEXT], bx
    mov bx, sn_i_hide
    test byte [STIK_PAY + SNP_FLAGS], SNF_DESK
    jnz .k
    mov bx, sn_i_show
.k:
    mov [sn_i_note + 2 * SNM_DESK], bx
    call sn_pencur              ; the Color menu: the current paper marked,
    xor cx, cx                  ; every one greyed on a 1bpp screen or with
    mov si, sn_i_col            ; no note to colour
.c:
    mov bx, cx
    mov ah, [sn_pens + bx]
    shl bx, 1
    mov bx, [sn_colstr + bx]    ; BX = its three strings: off, on, greyed
    cmp byte [sn_mono], 0
    jne .g
    cmp byte [STIK_PAY + SNP_N], 0
    je .g
    cmp ah, al
    jne .s
    add bx, SN_COLSZ
    jmp short .s
.g:
    add bx, 2 * SN_COLSZ
.s:
    mov [si], bx
    add si, 2
    inc cx
    cmp cx, SN_NCOL
    jb .c
    pop si
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; the About card (SPEC.md 12.2, 20.5.1), on the window it was asked on
; -----------------------------------------------------------------------------
sn_about:
    push bx
    push si
    call sn_abdismiss
    mov [sn_abwin], si
    mov bx, si
    mov si, sn_ablines
    call os88ui_about           ; arms the clip itself
    pop si
    pop bx
    ret

; sn_abdraw - the card over a window's paint, if it is that window's (SI)
sn_abdraw:
    cmp si, [sn_abwin]
    jne .r
    push bx
    push si
    mov bx, si
    mov si, sn_ablines
    call os88ui_about_d
    pop si
    pop bx
.r:
    ret

; sn_abdismiss - the card down if it is up: its window drawn again whole, and
; CF = 1 the event was that. Every register kept
sn_abdismiss:
    cmp word [sn_abwin], 0
    je .no
    push si
    mov si, [sn_abwin]
    mov word [sn_abwin], 0
    cmp si, [stik_win]
    jne .ed
    call stik_redraw
    jmp short .x
.ed:
    call sne_rall
.x:
    pop si
    stc
    ret
.no:
    clc
    ret

; --- the pad's window (SPEC.md 110.11, 110.13) --------------------------------
snp_tpl:
    dw 167, 260, 194, 172       ; stik_load sizes and places it (STIK_RIGHT)
    dw sn_title, snp_paint, snp_onkey, 0

sn_title:   db 'Sticky Note', 0

    OS88_MENUSET sn_menus, sn_title, sn_oncmd   ; AM_NAME NOT 0 (SPEC.md 110.9)
        OS88_MENU sn_m_note, sn_i_note, SNM_N
        OS88_MENU sn_m_col, sn_i_col, SN_NCOL
    OS88_MENUSET_END sn_menus
SNM_NEW     equ 0               ; the Note menu's items, in order (sn_cmds)
SNM_DEL     equ 1
SNM_BOX     equ 2
SNM_PREV    equ 3
SNM_NEXT    equ 4
SNM_EDIT    equ 5
SNM_DESK    equ 6
SNM_N       equ 7
sn_m_note:  db 'Note', 0
sn_m_col:   db 'Color', 0
sn_i_note:  dw sn_i_new, sn_i_del, sn_i_box, sn_i_prev, sn_i_next
            dw sn_i_edit, sn_i_hide
sn_i_new:   db 'New Note  Ctrl+N', 0
sn_i_del:   db 'Delete Note...', 0
sn_i_del0:  db MENU_DIS, 'Delete Note (none)', 0
sn_i_box:   db 'Check Box  Ctrl+T', 0
sn_i_box0:  db MENU_DIS, 'Check Box (no editor)', 0
sn_i_prev:  db 'Previous Note', 0
sn_i_prev0: db MENU_DIS, 'Previous Note (first)', 0
sn_i_next:  db 'Next Note', 0
sn_i_next0: db MENU_DIS, 'Next Note (last)', 0
sn_i_edit:  db 'Open Editor', 0
sn_i_hide:  db 'Hide from Desktop', 0
sn_i_show:  db 'Show on Desktop', 0
sn_i_col:   dw 0, 0, 0, 0, 0    ; sn_menulive fills these from sn_colstr
SN_COLSZ    equ 24              ; each colour's three strings, 24 bytes apart
%macro SN_COLOR 1
%%off:  db '  ', %1, 0
        times SN_COLSZ - ($ - %%off) db 0
%%on:   db '* ', %1, 0
        times SN_COLSZ - ($ - %%on) db 0
%%dis:  db MENU_DIS, %1, ' (color screen)', 0
        times SN_COLSZ - ($ - %%dis) db 0
%endmacro
sn_colstr:  dw sn_cy, sn_cp, sn_cg, sn_cb, sn_cw
sn_cy:      SN_COLOR 'Yellow'
sn_cp:      SN_COLOR 'Pink'
sn_cg:      SN_COLOR 'Green'
sn_cb:      SN_COLOR 'Blue'
sn_cw:      SN_COLOR 'White'

sn_s_full:  db '24 notes is the limit', 0
sn_s_pool:  db 'Notes are full (4 KB)', 0
sn_s_qdel:  db 'Delete this note?', 0

sn_ablines: dw sn_ab1, sn_ab2, sn_ab3, sn_ab4, 0
sn_ab1:     db 'Sticky Note', 0     ; each line inside the 192-px face
sn_ab2:     db 'Notes on the desk', 0
sn_ab3:     db 0
sn_ab4:     db 'SPEC.md 110.13', 0

%include "sntext.inc"

%define OS88UI_ABOUT
%define OS88UI_ALERT
%define OS88UI_NOGLYPH          ; no check-box glyph: ours are composed bit
                                ; records (SPEC.md 13.16.2.1)
%include "os88ui.inc"
%include "os88gfx.inc"
%include "os88stik.inc"
%include "snpad.inc"            ; AFTER os88stik.inc: STIK_LAYOUT is its macro
%include "sned.inc"             ; ...and os88gfx.inc: GFXE_BAND_ST is its

    align 16, db 0              ; os88stik.inc's read lands on a paragraph
    OS88_BSS STIK_BSS_END
    OS88_IMAGE_END
