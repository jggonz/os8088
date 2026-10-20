; =============================================================================
; os8088 - apps/stiknote/stiknote.asm
;
; STICKY NOTE - the second STICKY APP (SPEC.md 110, its own section 110.13).
; Notes and checklists on the desktop, kept where they were put and brought
; back at the next boot: it ships in STIKAPPS\ on the system disk and includes
; apps/os88stik.inc, which owns its place, its state file
; (SYSTEM\APPDATA\STIKNOTE.DAT) and its row in the desktop configuration.
;
; A NOTE IS A TEXT FILE: any *.TXT in SYSTEM\APPDATA\NOTES on the boot volume
; (docs/plans/completed/STIKNOTE-NOTEPAD-PLAN.md). Line 1 is its title, and a
; line beginning `[ ] ` or `[x] ` is a check box.
;
;   THE PAD      the program's one window - a DESK window (SPEC.md 11.103)
;                wearing os88stik.inc's widget treatment: ONE card, a sheet on
;                a stack of sheets with its corner turned, showing the
;                current note's top - a pin, its title, `+`, its lines and
;                boxes, and `< 1 / 3 >` with the open arrow. It reads the note
;                ON DEMAND: a turn of the page is one walk of the folder and
;                one 512-byte read (sntext.inc), and nothing else is held.
;   EDITING      is Note Pad's, or whatever program the user associated
;                .TXT with: the arrow (and `+`, once it has made the file)
;                hands the note to OSAPI_PKG_START's document arm with an
;                EMPTY name, from the pad's W_ONWAKE - the slot wants the gfx
;                lock FREE, and a click holds it. A snapshot of the instance
;                table before and after names the instance the launch made,
;                and it is watched at each beat - only while there is one -
;                and the pad reads its note again when it is gone.
;
; A tick is the file rewritten with the byte between the brackets flipped,
; Delete is the Desk Calendar's press-again latch, and a note's PAPER - one
; of five, a colour screen's - is the state file's: a table of the notes
; that are not yellow (SPEC.md 110.13.2).
;
;   stiknote.asm  this: header, payload, bss, entry, commands, menus, tick
;   sntext.inc    the notes: the folder walk, the head, the text's walk
;   snpad.inc     the pad: its three layouts, its composer, its presses
;
; Every pixel is COMPOSED into one 1bpp band and put down a slab at a time
; with OSAPI_GFX_BLIT1 (SPEC.md 110.11, os88gfx.inc) - no fill-then-letter,
; nothing written twice.
; =============================================================================

%define STIK_LIB                ; the Sticky App code is the ONE copy in
%define STIK_WALK               ; DESK.DRV (SPEC.md 110.15), and the notes'
                                ; walk uses its stik_enter .. stik_seek. Before
                                ; the header: OS88_STIKCARD_END lays its block
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

; --- the payload v2 (SPEC.md 110.13.2) ----------------------------------------
; tools/os88stik.py's STIKNOTE_* are the same numbers; t_stik holds them equal.
; A v1 file (the 4 KB pool) is DAMAGED to this build and replaced by the first
; save: nothing is carried over (the plan's decision A)
SNP_NAME    equ 0               ; 13: the current note's 8.3 name, NUL
SNP_FLAGS   equ 13              ; db reserved, 0
SNP_NC      equ 14              ; db the colour entries in use, 0..SN_MAXC
SNP_TAB     equ 15              ; SN_MAXC x SN_COLSZ: an 8-byte stem, space
SN_COLSZ    equ 9               ; padded, then its paper 1..SN_NCOL-1 (yellow,
SN_MAXC     equ 30              ; 0, is "absent")
SNP_LEN     equ SNP_TAB + SN_MAXC * SN_COLSZ    ; 285
SNP_VER     equ 2
SN_BOX0     equ 1               ; [it_k]: an open box...
SN_BOX1     equ 2               ; ...or a ticked one
SN_NCOL     equ 5               ; the papers (sn_pens)
SN_HEAD     equ 512             ; the bytes of a note the pad reads
SN_MAXKB    equ 16              ; a tick rewrites a note up to Note Pad's own
                                ; ceiling (NP_MAXKB) and refuses past it

KSC_DEL     equ 0x53
SN_C_NEW    equ 0x0E            ; Ctrl+N

; --- the Sticky App (apps/os88stik.inc, SPEC.md 110.5, 110.11) ---------------
%define STIK_DATNAME 'STIKNOTE.DAT'
%define STIK_CAP     'Sticky Note'
%define STIK_ISOPEN  'Sticky Note is open'
%define STIK_DMGREPLACE         ; a damaged file is replaced by the first save
%define STIK_ONTICK  sn_tick
%define STIK_DESKONLY           ; kern_big only: no titled fallback, no worker
                                ; arm (SPEC.md 110.13, the plan's decision F)
%define STIK_WAKE    sn_wake    ; the pad's W_ONWAKE: the editor's launch,
                                ; then the include's hover edge
STIK_PAYLEN equ SNP_LEN
STIK_PAYVER equ SNP_VER
STIK_ICON   equ 32
STIK_BSS    equ SN_BSS          ; the include's state follows ours
%define STIK_WIDGET
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
%define GFXE_BAND_W   STIK_FACE_W
%define GFXE_BAND_H   32
%define GFXE_BAND_BUF sn_band

; --- bss: OURS FIRST, at fixed offsets ----------------------------------------
%assign SNB 0
%macro SNV 2                    ; name, bytes
%1 equ os88_image_end + SNB
%assign SNB SNB + %2
%endmacro
    SNV sn_wz, 0                ; --- sn_walk zeroes these SN_WZ bytes:
    SNV sn_n, 1                 ; the notes (*.TXT) the last walk counted
    SNV sn_cur, 1               ; ...the current one's place among them
    SNV sn_hit, 1               ; ...it was found by NAME
    SNV sn_used, 4              ; ...NOTEnnnn numbers 1..32 in use, a bit each
    SNV sn_seen, 4              ; ...colour entries whose note it met
SN_WZ       equ SNB
    SNV sn_want, 1              ; the walk's other key: a place
    SNV sn_byo, 1               ; ...1: by that alone, not by name
    SNV sn_cz, 1                ; the current note's FIND +22 (compressed)
    SNV sn_hlen, 2              ; the head's bytes
    SNV sn_doc, 13              ; THE DOCUMENT: the current note's name...
    SNV sn_ndir, 2              ; ...NOTES' first cluster, BANKED (sn_go;
                                ; 0 = walk to it by name)...
    SNV sn_dvol, 1              ; ...and the volume: OSAPI_PKG_START's ES:DI
    SNV sn_stem, 8              ; sn_colfind's stem
    SNV sn_mono, 1              ; the primary is 1bpp: no colours
    SNV sn_ask, 1               ; Delete's latch: 2 armed by this event
    SNV sn_opq, 1               ; an open is owed to the wake
    SNV sn_wslot, 1             ; THE WATCH: the editor's instance slot + 1,
    SNV sn_wwin, 2              ; ...0 none; and its SSI_WIN
    SNV snp_press, 1            ; the pad element a press went to, SNP_H_*
    SNV snp_r0, 2               ; snp_pcard: the damaged rows...
    SNV snp_r1, 2
    SNV snp_ba, 2               ; ...the slab's first row and its rows
    SNV snp_bn, 2
    SNV it_p, 2                 ; THE WALK (sntext.inc): this line's start
    SNV it_e, 2                 ; ...the note's end
    SNV it_le, 2                ; ...this line's content end
    SNV it_n, 2                 ; ...the next line's start
    SNV it_k, 1                 ; ...the paragraph's kind: 0, SN_BOX0/1
    SNV it_f, 1                 ; ...1: the paragraph's first line (= it_k + 1)
    SNV it_lf, 1                ; ...it ended at a line end
    SNV it_end, 1               ; ...it ended at the note's end (= it_lf + 1)
    SNV it_w, 1                 ; ...the widths: a text line's, in cells
    SNV it_wb, 1                ; ...and a box line's
    SNV sn_t0, 2                ; scratch words
    SNV sn_t1, 2
    SNV sn_t2, 2
    SNV sn_t3, 2
    SNV sn_line, 8              ; `nn / nn`
    SNV sn_okey, 2              ; NOTES' bank (sn_ndir) is THIS disk's: our
                                ; own file's first cluster when it was made
    SNV sn_bank, 2              ; how sn_go came to NOTES: the bank's
                                ; cluster, or 0 by a walk
%assign SNB (SNB + 15) & ~15
    SNV sn_hbuf, SN_HEAD        ; the current note's top (a paragraph: the
                                ; compressed arm's OSAPI_FILE_READ wants one)
    SNV sn_band, GFXE_BAND_W / 8 * GFXE_BAND_H
SN_BSS      equ SNB             ; a multiple of 16
%if SN_BSS % 16
  %error "stiknote: its own bss must end on a paragraph"
%endif
%if SYS_SNAPSHOT_SIZE > GFXE_BAND_W / 8 * GFXE_BAND_H
  %error "stiknote: the instance snapshot is taken into the band"
%endif

; -----------------------------------------------------------------------------
; sn_entry - the entry proc (DS = CS = ours, ES = KERNEL_SEG, the UI task)
; in:  SI = the loader's name, for os88stik.inc
; out: CF = 0 launched, BX = the pad; CF = 1 not (another Sticky Note is up,
;      no desk layer, or no window)
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
    jnc .ok                     ; empty, and the first save replaces it
    push ds
    pop es
    mov di, STIK_PAY
    mov cx, SNP_LEN
    xor al, al
    cld
    rep stosb
    or byte [stik_st], STIK_S_DAMAGED
.ok:
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
    xor al, al                  ; the note the file names, or the first
    call sn_read
    call sn_menulive
    mov bx, [stik_win]
    clc
.out:
    pop di
    pop si
sn_ret:
    ret

; sn_check - CF = 1 the payload is not one we wrote: the colour entries'
; count and papers. The name is cut to its 13 bytes
sn_check:
    mov byte [STIK_PAY + SNP_NAME + 12], 0
    mov cl, [STIK_PAY + SNP_NC]
    cmp cl, SN_MAXC
    ja .bad
    mov bx, STIK_PAY + SNP_TAB + 8
.l:
    dec cl
    js .ok                      ; (CF = 0: DEC leaves it, CMP below cleared it)
    cmp byte [bx], SN_NCOL
    jae .bad
    add bx, SN_COLSZ
    jmp short .l
.ok:
    clc
    ret
.bad:
    stc
    ret

; -----------------------------------------------------------------------------
; sn_tick - STIK_ONTICK, every beat (the UI task, lock held, no clip). THE
; WATCH: only while an editor we launched is up is the instance table read,
; and when its slot is no longer live, or holds another window, the editor is
; gone and the pad reads its note again. Nothing is polled otherwise (the
; plan's decision 2). out: CF = 0, a save may go
; -----------------------------------------------------------------------------
sn_tick:
    mov cl, [sn_wslot]
    or cl, cl
    jz .x
    push cx
    call sn_live                ; the snapshot, into the band
    pop cx
    dec cx
    mov al, SSI_RECSZ
    mul cl
    xchg bx, ax
    cmp byte [sn_band + SS_INST + SSI_STATE + bx], 1
    jne .gone                   ; free, or dying (it saved before it closed)
    mov ax, [sn_band + SS_INST + SSI_WIN + bx]
    cmp ax, [sn_wwin]
    je .x
.gone:
    mov byte [sn_wslot], 0
    call sn_reread
.x:
    clc
    ret

; sn_live - the instance table (OSAPI_SYS_SNAPSHOT) into the band, which
; holds nothing between paints: AX = bit k set for each LIVE slot k. Spends
; BX, CX, DX, DI and ES
sn_live:
    push ds
    pop es
    mov di, sn_band
    call OSAPI_SYS_SNAPSHOT
    xor ax, ax
    mov dx, 1
    mov cx, INST_MAX
    mov di, sn_band + SS_INST
.l:
    cmp byte [di + SSI_STATE], 1
    jne .n
    or ax, dx
.n:
    shl dx, 1
    add di, SSI_RECSZ
    loop .l
    ret

; -----------------------------------------------------------------------------
; THE EDITOR: sn_open owes the wake a launch (a click holds the gfx lock and
; OSAPI_PKG_START wants it free, SPEC.md 21.5); sn_wake makes it
; -----------------------------------------------------------------------------
sn_open:
    cmp byte [sn_n], 0
    je .r
    mov byte [sn_opq], 1
    mov bx, [stik_win]
    call OSAPI_WM_WAKE
.r:
    ret

; sn_wake - STIK_WAKE: W_ONWAKE (SI = the pad, the UI task, NO lock). An open
; owed is made, then the include's hover edge runs as it always does
sn_wake:
    xor al, al
    xchg al, [sn_opq]
    or al, al
    jz .h
    test byte [stik_fl], STIK_F_QUIT
    jnz .h                      ; closing: nothing more
    call sn_launch
.h:
    jmp stik_wake

; sn_launch - the current note to whatever program .TXT is associated with
; (Note Pad, unless the user chose another): OSAPI_PKG_START's document arm,
; the name EMPTY (SPEC.md 21.5.3). The instance it made is whatever became
; LIVE across the call - every launch runs on the UI task, and the UI task is
; in here for all of it - so it is watched by its slot and its window. None
; (a refusal, said by the kernel; a program that keeps one instance) is no
; watch. Spends AX..DI and ES
sn_launch:
    cmp byte [sn_n], 0
    je .r
    call sn_gow                 ; STANDING IN NOTES: the association's run
    jc .r                       ; takes the document's folder from where the
                                ; caller stands (assoc_run_x's .curvol) -
                                ; and the editor WRITES there, so it is a
                                ; folder walked to by name (sn_gow)
    mov al, [stik_hbl]
    mov [sn_dvol], al           ; sn_doc, sn_ndir, sn_dvol: the locator
    call sn_live
    push ax
    push ds
    pop es
    mov di, sn_doc
    mov si, sn_empty            ; "": the document's extension names the
    xor cx, cx                  ; program
    xor dx, dx
    call OSAPI_PKG_START
    call stik_leave
    call sn_live
    pop bx
    not bx
    and ax, bx                  ; the slots that came alive
    jz .r
    xor cx, cx
.b:
    inc cx
    shr ax, 1
    jnc .b
    mov [sn_wslot], cl          ; its slot + 1
    dec cx
    mov al, SSI_RECSZ
    mul cl
    xchg bx, ax
    mov ax, [sn_band + SS_INST + SSI_WIN + bx]
    mov [sn_wwin], ax           ; ...and its window, which a compaction
.r:                             ; cannot move the way it moves a region
    ret

; -----------------------------------------------------------------------------
; the commands (the menus, the keys, the pad's presses): the UI task, lock
; held. Each spends AX..DI and ES, as a window callback may
; -----------------------------------------------------------------------------

; sn_age - an EVENT (a key, a press on the card, a menu pick): Delete's latch
; one event older (2 -> 1 -> 0), as the Desk Calendar's dc_age
sn_age:
    shr byte [sn_ask], 1
    ret

; sn_prev / sn_next / sn_turn - another note current, by its place AL
sn_prev:
    mov al, [sn_cur]
    or al, al
    jz sn_next.r
    dec ax
    jmp short sn_turn
sn_next:
    mov al, [sn_cur]
    inc ax
    cmp al, [sn_n]
    jb sn_turn
.r:
    ret
sn_turn:
    mov [sn_want], al
    mov al, 1
    jmp short sn_show

; sn_reread - the current note again, by its name (by its place when the name
; is gone), and the pad
sn_reread:
    mov al, [sn_cur]
    mov [sn_want], al
    xor al, al
; sn_show - sn_read (AL as its), then the pad and the menus
sn_show:
    call sn_read
; sn_paint - the pad whole and the menus, as the state stands
sn_paint:
    call snp_redraw
    jmp sn_menulive

; sn_new - a new note: NOTEnnnn.TXT, the lowest number free, holding a title;
; it is current, and it opens in the editor as the arrow does
sn_new:
    call sn_gow                 ; a CREATE: where a walk says (sn_go)
    jc .nos
    mov al, [sn_cur]            ; the numbers in use NOW, and the current
    mov [sn_want], al           ; note kept
    mov byte [sn_byo], 0
    call sn_walk
    jc .noh
    mov ax, [sn_used]           ; the lowest clear bit of the 32: AX = its
    mov dx, [sn_used + 2]       ; number, 33 when there is none
    xor cx, cx
.f:
    inc cx
    shr dx, 1
    rcr ax, 1
    jc .f
    xchg ax, cx
    cmp al, 32
    ja .full
    aam                         ; AH = the tens, AL = the units
    add ax, '00'
    xchg al, ah
    mov [sn_nname + 6], ax
    mov si, sn_nname
    push ds
    pop es
    mov bx, sn_newtxt
    mov cx, SN_NEWLEN
    xor dx, dx
    call OSAPI_FILE_WRITE
    jnc .made
    cmp ax, FERR_DIRFULL
    jne .noh
.full:
    mov si, sn_s_full
    jmp short .say
.made:
    push ds
    pop es
    mov si, sn_nname
    call sn_setname
    call sn_rdin                ; its place, the count, its head; home
    call sn_paint
    jmp sn_open
.noh:
    mov si, sn_s_nos
.say:
    call stik_leave
    jmp short sn_toast
.nos:
    mov si, sn_s_nos
; sn_toast - SI = a line (DS)
sn_toast:
    push ds
    pop es
    xor cx, cx
    call OSAPI_TOAST
    ret

; sn_askdel - Del / Delete Note, TWICE: the first arms the latch and says
; so, a second on the very next event deletes the note (the Desk Calendar's
; dc_askdel, the plan's decision C - no alert)
sn_askdel:
    cmp byte [sn_n], 0
    je .r
    cmp byte [sn_ask], 1        ; armed by the event before this one
    je sn_delete
    mov byte [sn_ask], 2
    mov si, sn_s_qdel
    jmp short sn_toast
.r:
    ret

; sn_delete - the current note's file deleted; the note now in its place is
; current
sn_delete:
    mov byte [sn_ask], 0
    call sn_go
    jc sn_paint
    mov si, STIK_PAY + SNP_NAME
    call OSAPI_FILE_DELETE
    mov al, [sn_cur]
    mov [sn_want], al
    mov byte [sn_byo], 0
    call sn_rdin
    jmp sn_paint

; -----------------------------------------------------------------------------
; sn_flip - a press on a box: the file read whole into a claim, the byte
; between the brackets flipped (space <-> x), written back, the claim freed -
; which works on a compressed note too. The walk is on the box's line ([it_p]
; its text, [sn_t1] the pad's line). Anything not as the head said - the file
; changed under us - is a fresh read and the pad whole; a note over 16 KB (not
; one Note Pad made) is refused in words. Last save wins (decision 3)
; -----------------------------------------------------------------------------
sn_flip:
    mov ax, [it_p]
    sub ax, sn_hbuf + 3         ; the byte between the brackets, a file offset
    mov [sn_t0], ax
    call sn_go
    jc .r
    xor bx, bx                  ; THE SIZE PROBE: no room, so FERR_BIG and
    xor cx, cx                  ; DX = the KB the read needs (SPEC.md 18.4.1)
    xor dx, dx
    mov si, STIK_PAY + SNP_NAME
    push ds
    pop es
    call OSAPI_FILE_READ
    jnc .re                     ; empty: not what the head said
    cmp ax, FERR_BIG
    jne .re
    mov si, sn_s_big
    cmp dx, SN_MAXKB
    ja .say
    xchg ax, dx
    call OSAPI_MEM_CLAIM        ; DX = it; AX kept
    jc .ns
    push dx
    mov es, dx
    xor bx, bx
    xor cx, cx
    mov ch, al
    shl ch, 1
    shl ch, 1                   ; CX = the KB x 1024
    xor dx, dx
    mov si, STIK_PAY + SNP_NAME
    call OSAPI_FILE_READ        ; DX:AX = its size
    jc .free
    xchg cx, ax                 ; CX = its size
    mov bx, [sn_t0]
    lea ax, [bx + 2]
    cmp ax, cx
    ja .free                    ; the box is past its end now
    mov al, [es:bx]
    cmp al, ' '
    mov ah, 'x'                 ; open: ticked
    je .bk
    or al, 20h
    cmp al, 'x'
    jne .free
    mov ah, ' '                 ; ticked: open
.bk:
    cmp byte [es:bx - 1], '['
    jne .free
    cmp byte [es:bx + 1], ']'
    jne .free
    mov [es:bx], ah
    mov [sn_hbuf + bx], ah
    xor bx, bx                  ; ES:BX = the note, DX:CX its size
    xor dx, dx
    mov si, STIK_PAY + SNP_NAME
    call OSAPI_FILE_WRITE
    pop dx
    pushf
    call OSAPI_MEM_FREE
    popf
    jc .ns2
    call stik_leave
    jmp snp_rline               ; the pad's line [sn_t1] again
.free:
    pop dx
    call OSAPI_MEM_FREE
.re:
    call sn_rdin                ; not as the head said: read it again
    jmp sn_paint
.ns2:
    call sn_rdin                ; the write was refused: what it holds
    call sn_paint
    mov si, sn_s_nos
    jmp sn_toast
.ns:
    mov si, sn_s_nos
.say:
    call stik_leave
    jmp sn_toast
.r:
    ret

; -----------------------------------------------------------------------------
; THE PAPERS
; -----------------------------------------------------------------------------

; sn_setcol - the Color menu: the current note's paper AL (0..4)
sn_setcol:
    cmp byte [sn_mono], 0
    jne .r                      ; greyed: a shortcut never comes here either
    cmp byte [sn_n], 0
    je .r
    push ax
    mov si, sn_doc
    call sn_colfind             ; BX = its entry, CL its index - or CF
    pop ax
    jc .new
    or al, al
    jz .del                     ; yellow is "absent"
    mov [bx + 8], al
    jmp short .t
.del:
    call sn_coldel
    jmp short .t
.new:
    or al, al
    jz .r                       ; yellow already
    cmp cl, SN_MAXC
    jae .r                      ; thirty colours on the desk: no thirty-first
    inc byte [STIK_PAY + SNP_NC]
    push ds
    pop es
    mov si, sn_stem
    mov di, bx
    mov cx, 4
    cld
    rep movsw
    stosb
.t:
    call stik_touch
    jmp sn_paint
.r:
    ret

; sn_pencur - AL = the paper of the CURRENT note (CWHITE with no notes).
; Every other register kept
sn_pencur:
    push bx
    push cx
    push si
    push di
    mov al, CWHITE
    cmp byte [sn_n], 0
    je .x
    mov si, sn_doc
    call sn_colfind             ; BX = its entry, or CF: yellow
    mov al, CYELLOW
    jc .x
    mov bl, [bx + 8]
    xor bh, bh
    mov al, [sn_pens + bx]
.x:
    pop di
    pop si
    pop cx
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
; the menus (SPEC.md 12.2): AL = the item, AH = the menu, SI = the window;
; lock held
; -----------------------------------------------------------------------------
sn_oncmd:
    call sn_age
    cmp ah, 1
    je .col
    xor ah, ah
    mov bx, ax
    shl bx, 1
    jmp [sn_cmds + bx]
.col:
    jmp sn_setcol

sn_cmds:    dw sn_new, sn_askdel, sn_prev, sn_next, sn_open

; sn_menulive - the items greyed and labelled as the state stands (SPEC.md 47).
; Spends AX, BX, CX, SI
sn_menulive:
    mov al, [sn_n]
    mov bx, sn_i_del
    mov cx, sn_i_open
    or al, al
    jnz .d
    mov bx, sn_i_del0
    mov cx, sn_i_open0
.d:
    mov [sn_i_note + 2 * SNM_DEL], bx
    mov [sn_i_note + 2 * SNM_OPEN], cx
    mov bx, sn_i_prev
    cmp byte [sn_cur], 0
    jne .p
    mov bx, sn_i_prev0
.p:
    mov [sn_i_note + 2 * SNM_PREV], bx
    mov bx, sn_i_next
    mov ah, [sn_cur]
    inc ah
    cmp ah, al
    jb .n
    mov bx, sn_i_next0
.n:
    mov [sn_i_note + 2 * SNM_NEXT], bx
    call sn_pencur              ; the Color menu: the current paper marked,
    mov dl, [sn_mono]           ; every one greyed on a 1bpp screen or with
    cmp byte [sn_n], 0          ; no note to colour (DL = 0 live)
    jne .m
    inc dx
.m:
    xor cx, cx
    mov si, sn_i_col
.c:
    mov bx, cx
    mov ah, [sn_pens + bx]
    shl bx, 1
    mov bx, [sn_colstr + bx]    ; BX = its item: two marks, the name, the
    lea di, [bx + 2]            ; why - the name's end is its first byte
.z:                             ; at or below a space
    inc di
    cmp byte [di], ' '
    ja .z
    or dl, dl
    jnz .g
    mov word [bx], '  '         ; `  Pink`, or `* Pink`
    mov byte [di], 0
    cmp ah, al
    jne .s
    mov byte [bx], '*'
    jmp short .s
.g:
    inc bx                      ; greyed: `Pink (color screen)`
    mov byte [bx], MENU_DIS
    mov byte [di], ' '
.s:
    mov [si], bx
    inc si
    inc si
    inc cx
    cmp cx, SN_NCOL
    jb .c
    ret

; sn_about - the About item (SPEC.md 12.2, 20.5.1): one toast, as the Desk
; Calendar's, and the pad is never covered. W_ONCLICK's terms: SI given back
sn_about:
    push si
    mov si, sn_s_about
    call sn_toast
    pop si
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
SNM_PREV    equ 2
SNM_NEXT    equ 3
SNM_OPEN    equ 4
SNM_N       equ 5
sn_m_note:  db 'Note', 0
sn_m_col:   db 'Color', 0
sn_i_note:  dw sn_i_new, sn_i_del, sn_i_prev, sn_i_next, sn_i_open
sn_i_new:   db 'New Note  Ctrl+N', 0
sn_i_del:   db 'Delete Note  Del', 0
sn_i_del0:  db MENU_DIS, 'Delete Note (none)', 0
sn_i_prev:  db 'Previous Note', 0
sn_i_prev0: db MENU_DIS, 'Previous Note (first)', 0
sn_i_next:  db 'Next Note', 0
sn_i_next0: db MENU_DIS, 'Next Note (last)', 0
sn_i_open:  db 'Open Note  Enter', 0
sn_i_open0: db MENU_DIS, 'Open Note (none)', 0
sn_i_col:   dw 0, 0, 0, 0, 0    ; sn_menulive fills these from sn_colstr
; ONE string a paper, three ways (sn_menulive): `  Name` and `* Name` with the
; byte after the name a NUL, and `Name (color screen)` greyed - one in, its
; mark byte MENU_DIS and that NUL a space
%macro SN_COLOR 1
    db '  ', %1, 0, '(color screen)', 0
%endmacro
sn_colstr:  dw sn_cy, sn_cp, sn_cg, sn_cb, sn_cw
sn_cy:      SN_COLOR 'Yellow'
sn_cp:      SN_COLOR 'Pink'
sn_cg:      SN_COLOR 'Green'
sn_cb:      SN_COLOR 'Blue'
sn_cw:      SN_COLOR 'White'

sn_dnotes:  db 'NOTES', 0
sn_nname:   db 'NOTE00nn.TXT', 0        ; sn_new writes the number in
sn_newtxt:  db 'New note', 13, 10       ; line 1, the title: a write wants a
SN_NEWLEN   equ $ - sn_newtxt           ; byte, and Note Pad gets a line
sn_s_full:  db 'Notes folder is full', 0
sn_s_nos:   db 'Note not saved', 0
sn_s_big:   db 'Note is over 16 KB', 0
sn_s_qdel:  db 'Delete again to confirm', 0

sn_s_about: db 'Sticky Note for os8088', 0
sn_empty:   db 0                    ; OSAPI_PKG_START's empty program name

%include "sntext.inc"

%define OS88UI_MACROS           ; no os88ui control: About is a toast (the
                                ; Desk Calendar's arrangement)
%include "os88ui.inc"
%include "os88gfx.inc"
%include "os88stik.inc"
%include "snpad.inc"            ; AFTER os88stik.inc: STIK_LAYOUT is its macro

    align 16, db 0              ; os88stik.inc's read lands on a paragraph
    OS88_BSS STIK_BSS_END
    OS88_IMAGE_END
