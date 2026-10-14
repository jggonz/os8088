; =============================================================================
; tests/diaprobe/diaprobe.asm - the smallest DESKTOP INTEGRATED APPLICATION
; (SPEC.md 107)
;
; Not shipped software: the gate package `make diatest` puts in SYSTEM\DIA\
; of its fixture system disks. It is apps/os88dia.inc and a window, nothing
; else, so every row that boots it asks about the MECHANISM and not about an
; application:
;
;   diaboot    SYSTEM.CFG's DIA row launches it at boot, where DIAPROBE.DAT
;              says its window was, and it holds no desktop cell
;   diamiss    a row beside a missing one still launches
;   diahiber   ...and is NOT launched under a pending resume question
;
; It registers its row on its first tick (a byte-identical row is a no-op
; that writes nothing) and removes it BY NAME on close. Its payload is EMPTY:
; the DIA header alone is its state, DIAPROBE.DAT is 16 bytes.
;
; **THE DESK BUILD, `-DDP_DESK`** (SPEC.md 11.103, v2's wave 1) is the same
; source as DESKPROB.O88, 'DeskProbe', DESKPROB.DAT: header flag OS88_F_DESK,
; OSAPI_WM_DESK's QUERY and MAKE in the entry proc, and a face of TWO WHITE
; CARDS with the desktop's own ground between them, below them and down the
; frame's left column, through OSAPI_GFX_DESK - every pixel once. It counts
; its presses, releases and keys in its bss for desk*.py to read, and gives
; the bar back (RELEASE) on Esc. Where QUERY refuses (kern_small) it is the
; TITLED FALLBACK: the same face under an ordinary title bar (SPEC.md 107.7).
; It is its own build, not DIAPROBE's, because the v1 rows drag DiaProbe by
; its title bar and close it with its close box (SPEC.md 107.9).
;
; WAVE 2 (SPEC.md 11.103.6-7) gives it a crude TAG ROW under the cards: at
; rest a white plate on the desktop's ground; while the kernel's hover edge
; says WF_HOVER (its W_ONWAKE reads the bit, and refuses to REVEAL with a
; button held) a black HANDLE with a white close box at its left. A press on
; anything that is not a card - a gap, the tag, a ground column - posts
; OSAPI_WM_DESK DRAG; a press on the revealed box arms it (lit), and its
; release ON the box closes the probe through the include's own negotiator
; and OSAPI_WM_CLOSE, as a widget's x must (SPEC.md 107.11). Its DIA_ONTICK
; posts a DRAG from the TIMER on request, which must be inert.
; =============================================================================

%include "os88api.inc"

%ifdef DP_DESK
    OS88_HEADER 'DeskProbe', dp_entry, OS88_F_ICON | OS88_F_DESK
%else
    OS88_HEADER 'DiaProbe', dp_entry, OS88_F_ICON
%endif

    OS88_ICON16
    dw 0xFFFF                       ; 16 mask rows: the whole square white
    times 15 dw 0xFFFF
    dw 0xFFFF                       ; 16 data rows: a framed box with a dot
    dw 0x8001, 0x8001, 0x8001, 0x8001, 0x8001, 0x8181, 0x83C1
    dw 0x83C1, 0x8181, 0x8001, 0x8001, 0x8001, 0x8001, 0x8001
    dw 0xFFFF
    OS88_ICON16_END

%ifdef DP_DESK
%define DIA_DATNAME 'DESKPROB.DAT'
%define DIA_CAP     'DeskProbe'
%else
%define DIA_DATNAME 'DIAPROBE.DAT'
%define DIA_CAP     'DiaProbe'
%endif
DIA_PAYLEN  equ 0
DIA_PAYVER  equ 1
DIA_ICON    equ 32
%ifdef DP_DESK
DIA_BSS     equ 32                  ; the probe's own counters, then the
                                    ; include's state (paragraphs)
%else
DIA_BSS     equ 0
%endif

%ifdef DP_DESK
; --- the face (SPEC.md 107.11's shape, in miniature) --------------------------
DPF_W       equ 128                 ; the face: two cards, 128 wide
DPF_C1      equ 40                  ; card 1, rows 0..39
DPF_GAP     equ 8                   ; then a gap of ground
DPF_C2      equ 40                  ; card 2
DPF_GAP2    equ 4                   ; a gap under it
DPF_TAG     equ 12                  ; ...and the TAG ROW (wave 2)
DPF_TY      equ DPF_C1 + DPF_GAP + DPF_C2 + DPF_GAP2    ; its first row
DPF_H       equ DPF_TY + DPF_TAG
DPF_PX      equ 32                  ; the rest plate: face x 32..95
DPF_PW      equ 64
DPF_XX      equ 3                   ; the handle's close box: face x 3..12,
DPF_XW      equ 10                  ; tag rows 1..10
DP_DW       equ DPF_W + 2           ; the desk frame: a ground column either
                                    ; side of the face - wm_create's SIZE snap
                                    ; puts W_W at 8k+2 (SPEC.md 11.94.5), so
                                    ; the far column is the frame's too
DP_DH       equ DPF_H
DP_TW       equ DPF_W + 2           ; the titled fallback: the SAME width - the
                                    ; two borders stand where the desk form's
                                    ; ground columns do, so the face is at
                                    ; W_X + 1 in both (SPEC.md 107.11)
DP_TH       equ DPF_H + TITLE_H + 1 ; ...and the bar and the bottom border

; the probe's bss, read by tests/desk*.py at image + these offsets
dp_clicks   equ os88_image_end + 0  ; byte: W_ONCLICKs taken
dp_ups      equ os88_image_end + 1  ; byte: W_ONMOUSEUPs taken
dp_keys     equ os88_image_end + 2  ; byte: W_ONKEYs taken (Esc included)
dp_desk     equ os88_image_end + 3  ; byte: 1 = MAKE said yes, a desk window
dp_lastkey  equ os88_image_end + 4  ; word: the last key's AX
dp_paints   equ os88_image_end + 6  ; word: W_PAINTs
dp_rel      equ os88_image_end + 8  ; byte: RELEASE's CF on the last Esc + 1
dp_wakes    equ os88_image_end + 9  ; byte: W_ONWAKEs taken (the hover edge)
dp_rev      equ os88_image_end + 10 ; byte: 1 = the handle is REVEALED
dp_xarm     equ os88_image_end + 11 ; byte: 1 = a press on the box is armed
dp_drags    equ os88_image_end + 12 ; byte: DRAGs posted from W_ONCLICK
dp_tdrag    equ os88_image_end + 13 ; byte: 1 = post one from the TIMER (the
                                    ; test sets it), 2 = it was posted
dp_closes   equ os88_image_end + 14 ; byte: closes asked by the box
dp_tags     equ os88_image_end + 15 ; byte: tag rows drawn OUTSIDE W_PAINT
dp_ondrags  equ os88_image_end + 16 ; byte: W_ONDRAGs taken - a posted drag
                                    ; must deliver NONE (it arms nothing)
%define DIA_ONTICK dp_ontick
%endif

dp_entry:
    push si
    push di
    mov di, dp_tpl
%ifdef DP_DESK
    mov al, 0                   ; QUERY first, so the template is the right
    call OSAPI_WM_DESK          ; size for the form it will take
    jnc .q
    mov word [dp_tpl+WT_W], DP_TW
    mov word [dp_tpl+WT_H], DP_TH
.q:
%endif
    call dia_load               ; SI = the loader's name; places the template
    jc .out                     ; ...or another instance is up: say no
    mov si, dp_tpl
    call OSAPI_WM_CREATE
    jc .out
%ifdef DP_DESK
    mov al, 1                   ; MAKE: inside the entry proc, the window
    call OSAPI_WM_DESK          ; hidden and nobody's yet (SPEC.md 11.103.9)
    jc .titled
    mov byte [dp_desk], 1
    jmp short .made
.titled:
    mov al, 1                   ; the fallback paints every pixel of its
    call OSAPI_WM_OWNBG         ; content too: no white fill under the face
.made:
    mov ax, dp_up
    call OSAPI_WM_ONMOUSEUP
    mov ax, dp_ondrag           ; counted: an armed card press may deliver
    call OSAPI_WM_ONDRAG        ; some, a posted DRAG none (CF=1 on small)
    mov ax, dp_wake             ; the hover edge's wake (SPEC.md 11.103.7):
    call OSAPI_WM_ONWAKE        ; free on kern_big, where dia_made takes the
                                ; timer; kern_small's worker arm overwrites it
%endif
    call dia_made
    clc
.out:
    pop di
    pop si
    ret

%ifdef DP_DESK
; -----------------------------------------------------------------------------
; dp_paint - the face: the ground column, card 1, a gap, card 2, a gap. Every
; pixel once - the ground through OSAPI_GFX_DESK, which leaves the THEME'S
; colour in the pen, so each card sets white again (SPEC.md 11.103.9)
; -----------------------------------------------------------------------------
dp_paint:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    inc word [dp_paints]
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = left, DX = top: (W_X, W_Y) for a desk
    mov bp, dx                  ; window, inside the title for the fallback
    mov si, ax                  ; SI = x0 - the column LEFT of the face
    cmp byte [dp_desk], 0
    jne .desk
    dec si                      ; the fallback: the content IS the face, and
    jmp short .cards            ; its borders stand where the columns would
.desk:
    mov bx, bp                  ; the ground columns, the face's full height
    mov cx, si
    mov dx, bp
    add dx, DPF_H - 1
    call OSAPI_GFX_DESK         ; ...the left one, at x0
    add ax, DPF_W + 1
    mov cx, ax
    call OSAPI_GFX_DESK         ; ...and the right one, at x0 + DPF_W + 1
.cards:
    mov dx, 0                   ; card 1
    mov cx, DPF_C1
    call dp_card
    add dx, DPF_C1              ; gap 1
    mov cx, DPF_GAP
    call dp_gap
    add dx, DPF_GAP             ; card 2
    mov cx, DPF_C2
    call dp_card
    add dx, DPF_C2              ; gap 2
    mov cx, DPF_GAP2
    call dp_gap
    call dp_tagrow              ; the tag row, by [dp_rev]
    call dia_painted            ; kern_small: hire the worker (SPEC.md 107.5.2)
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dp_band - a face-wide band: in SI = x0, BP = y0, DX = its first row (face-
; relative), CX = its rows; out AX,BX..CX,DX = its rect, absolute and
; inclusive. dp_card and dp_gap bank DX round it
dp_band:
    mov ax, si
    inc ax                      ; the face starts one column in
    mov bx, bp
    add bx, dx                  ; y1 = y0 + row0
    mov dx, bx
    add dx, cx
    dec dx                      ; y2 = y1 + rows - 1
    mov cx, si
    add cx, DPF_W               ; x2 = x0 + 1 + DPF_W - 1
    ret

dp_card:
    push dx
    push ax
    mov al, CWHITE
    call OSAPI_SET_COLOR
    pop ax
    call dp_band
    call OSAPI_GFX_FILL
    pop dx
    ret

dp_gap:
    push dx
    call dp_band
    call OSAPI_GFX_DESK
    pop dx
    ret

; dp_tagrow - the tag row, rest or handle by [dp_rev]: in SI = x0 (the column
; left of the face), BP = y0 (the face's top). Every pixel once: the rest is
; ground | plate | ground, the handle bar | box column (bar, box, bar) | bar
dp_tagrow:
    push ax
    push bx
    push cx
    push dx
    cmp byte [dp_rev], 0
    jne .hand
    mov ax, 0                   ; ground, face x 0..PX-1
    mov cx, DPF_PX
    call dp_tagfill_g
    mov ax, DPF_PX              ; the white plate
    mov cx, DPF_PW
    mov dl, CWHITE
    call dp_tagfill_c
    mov ax, DPF_PX + DPF_PW     ; ground to the face's right edge
    mov cx, DPF_W - DPF_PX - DPF_PW
    call dp_tagfill_g
    jmp short .out
.hand:
    mov ax, 0                   ; the bar left of the box
    mov cx, DPF_XX
    mov dl, CBLACK
    call dp_tagfill_c
    mov ax, DPF_XX              ; the box's column: its top row black...
    mov bx, 0
    mov cx, 1
    call dp_boxfill_b
    mov bx, 1                   ; ...ten rows of white...
    mov cx, DPF_TAG - 2
    mov dl, CWHITE
    call dp_boxfill
    mov bx, DPF_TAG - 1         ; ...and its bottom row black
    mov cx, 1
    call dp_boxfill_b
    mov ax, DPF_XX + DPF_XW     ; the bar right of the box
    mov cx, DPF_W - DPF_XX - DPF_XW
    mov dl, CBLACK
    call dp_tagfill_c
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dp_tagfill_g / _c - the tag row's full height over face x AX .. AX+CX-1:
; the ground (_g), or colour DL (_c). SI = x0, BP = y0
dp_tagfill_g:
    push ax
    push bx
    push cx
    push dx
    call dp_tagrect
    call OSAPI_GFX_DESK
    jmp short dp_tf_out
dp_tagfill_c:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov al, dl
    call OSAPI_SET_COLOR
    pop ax
    call dp_tagrect
    call OSAPI_GFX_FILL
dp_tf_out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dp_tagrect - in AX = face x, CX = width; out AX..DX the tag row's rect over
; them, absolute and inclusive
dp_tagrect:
    add ax, si
    inc ax                      ; the face starts one column in
    add cx, ax
    dec cx
    mov bx, bp
    add bx, DPF_TY
    mov dx, bx
    add dx, DPF_TAG - 1
    ret

; dp_boxfill(_b) - the close box's column, DPF_XW wide at face x AX: tag rows
; BX .. BX+CX-1 in colour DL (_b: black)
dp_boxfill_b:
    mov dl, CBLACK
dp_boxfill:
    push ax
    push bx
    push cx
    push dx
    push ax
    mov al, dl
    call OSAPI_SET_COLOR
    pop ax
    call dp_boxrect
    call OSAPI_GFX_FILL
    jmp short dp_tf_out

; dp_boxrect - in AX = face x, BX = the first tag row, CX = rows; out AX..DX
; the rect, DPF_XW wide
dp_boxrect:
    add ax, si
    inc ax
    add bx, bp
    add bx, DPF_TY
    mov dx, bx
    add dx, cx
    dec dx
    mov cx, ax
    add cx, DPF_XW - 1
    ret

; dp_org - SI = the window: out SI = x0 (the column left of the face), BP =
; y0 (the face's top). dp_paint's own arithmetic, for the callbacks
dp_org:
    push ax
    push bx
    push dx
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov si, ax
    mov bp, dx
    cmp byte [dp_desk], 0
    jne .d
    dec si                      ; the fallback: its border is the column
.d:
    pop dx
    pop bx
    pop ax
    ret

; dp_face - CX, DX = a screen point, SI = the window: out CX, DX = it FACE-
; relative (fx may be -1 or DPF_W: a ground column)
dp_face:
    push si
    push bp
    call dp_org
    sub cx, si
    dec cx
    sub dx, bp
    pop bp
    pop si
    ret

; dp_inbox - CX, DX face-relative: CF = 1 on the handle's close box
dp_inbox:
    cmp dx, DPF_TY + 1
    jl .no
    cmp dx, DPF_TY + DPF_TAG - 2
    jg .no
    cmp cx, DPF_XX
    jl .no
    cmp cx, DPF_XX + DPF_XW - 1
    jg .no
    stc
    ret
.no:
    clc
    ret

; dp_xlit - invert the close box's white interior (the press's light, and
; its undoing). SI = the window, lock held
dp_xlit:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    call dp_org
    mov ax, DPF_XX
    mov bx, 1
    mov cx, DPF_TAG - 2
    call dp_boxrect
    call OSAPI_GFX_XOR_FILL
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dp_click - W_ONCLICK, SI = the window, CX/DX the press. A CARD counts it;
; anything else on the desk form is the frame's: the revealed box arms, and
; the rest posts the kernel's DRAG (SPEC.md 11.103.6)
dp_click:
    push ax
    push bx
    push cx
    push dx
    call dp_face
    cmp cx, DPF_W               ; unsigned: -1 and DPF_W are the columns
    jae .frame
    cmp dx, DPF_C1
    jb .card
    cmp dx, DPF_C1 + DPF_GAP
    jb .frame
    cmp dx, DPF_C1 + DPF_GAP + DPF_C2
    jb .card
.frame:
    cmp byte [dp_desk], 0
    je .card                    ; the fallback has a title bar to drag by
    cmp byte [dp_rev], 0
    je .drag
    call dp_inbox
    jnc .drag
    mov byte [dp_xarm], 1       ; the box: armed and lit, acted on at the
    call dp_xlit                ; release
    jmp short .out
.drag:
    mov bx, si
    mov al, 2                   ; DRAG: the kernel moves us once we return
    call OSAPI_WM_DESK
    jc .out
    inc byte [dp_drags]
    jmp short .out
.card:
    inc byte [dp_clicks]
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dp_up - W_ONMOUSEUP, SI = the window, CX/DX the release, lock held. The box
; acts only if the release is ON it - a slide off cancels (SPEC.md 13.5)
dp_up:
    cmp byte [dp_xarm], 0
    jne .x
    inc byte [dp_ups]
    ret
.x:
    push ax
    push bx
    push cx
    push dx
    mov byte [dp_xarm], 0
    call dp_xlit                ; the light off first, either way
    call dp_face
    call dp_inbox
    jnc .stay
    inc byte [dp_closes]
    call dia_close              ; the include's negotiator: the row away by
    jc .stay                    ; name, the place saved - CF = 1 declined
    mov bx, si
    call OSAPI_WM_CLOSE         ; ...and closed; nothing drawn after this
    jmp short .out
.stay:
    call dp_sync                ; whatever the pointer did while it was held
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dp_ondrag:                      ; W_ONDRAG: counted, nothing else
    inc byte [dp_ondrags]
    ret

; dp_wake - W_ONWAKE, SI = the window, the UI task, NO lock: the kernel's
; hover edge (SPEC.md 11.103.7). An armed box keeps its picture until its
; release, which reconciles
dp_wake:
    inc byte [dp_wakes]
    cmp byte [dp_xarm], 0
    jne .r
    call OSAPI_GFX_LOCK
    call dp_sync
    call OSAPI_GFX_UNLOCK
.r:
    ret

; dp_sync - lock held, SI = the window: the tag row to what WF_HOVER says.
; Revealed while hovered; REVEALED anew only with no button held (the
; package's refusal, SPEC.md 11.103.7). Drawn only on a change, clipped
dp_sync:
    push ax
    push bx
    push cx
    push dx
    push si
    push bp
    xor ah, ah                  ; AH = what it should be
    test byte [es:si+W_FLAGS+1], WF_HOVER >> 8
    jz .want
    inc ah
    cmp byte [dp_rev], 0
    jne .want                   ; already up: it stays up while hovered
    push ax
    call OSAPI_MOUSE            ; AL = the buttons
    test al, 3
    pop ax
    jz .want
    dec ah                      ; a button held: never revealed
.want:
    cmp ah, [dp_rev]
    je .done
    mov [dp_rev], ah
    test byte [es:si+W_FLAGS], 2
    jz .done                    ; not on the glass: the next paint has it
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .done                    ; nothing of it showing
    inc byte [dp_tags]
    call dp_org
    call dp_tagrow
.done:
    pop bp
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; dp_ontick - DIA_ONTICK, the beat (the timer, lock held): a DRAG posted from
; HERE when the test asks, which the next press must not turn into a drag
dp_ontick:
    cmp byte [dp_tdrag], 1
    jne .r
    push ax
    push bx
    mov bx, [dia_win]
    mov al, 2
    call OSAPI_WM_DESK
    mov byte [dp_tdrag], 2
    pop bx
    pop ax
.r:
    clc
    ret

dp_key:                         ; AL = ascii, AH = scan, SI = the window
    inc byte [dp_keys]
    mov [dp_lastkey], ax
    cmp al, 27
    jne .x
    push ax
    push bx
    mov bx, si
    mov al, 3                   ; RELEASE: the bar and the keys go back
    call OSAPI_WM_DESK
    mov al, 1
    adc al, 0                   ; 1 = CF 0, 2 = CF 1
    mov [dp_rel], al
    pop bx
    pop ax
.x:
    ret

dp_tpl:
    dw 40, 60, DP_DW, DP_DH     ; the TEMPLATE place: a valid state file
    dw dp_ttl, dp_paint, dp_key, dp_click   ; moves it

dp_ttl:     db 'DeskProbe', 0
%else
dp_paint:                       ; SI = the window, lock held: one opaque run
    push ax
    push bx
    push cx
    push dx
    push si
    mov bx, si
    call OSAPI_WM_CONTENT       ; AX = left, DX = top
    mov cx, ax
    add cx, 8
    add dx, 8
    mov si, dp_msg
    mov ax, 0x0F00              ; ink 0 on 15: two colours, every adapter
    call OSAPI_FONT_RUN
    call dia_painted            ; kern_small: hire the worker (SPEC.md 107.5.2)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dp_tpl:
    dw 40, 60, 160, 50          ; the TEMPLATE place: a valid state file
    dw dp_ttl, dp_paint, 0, 0   ; moves it

dp_ttl:     db 'DiaProbe', 0
dp_msg:     db 'DIA probe', 0
%endif

%include "os88dia.inc"

    align 16, db 0              ; os88dia.inc's read lands on a paragraph
    OS88_BSS DIA_BSS_END
    OS88_IMAGE_END
