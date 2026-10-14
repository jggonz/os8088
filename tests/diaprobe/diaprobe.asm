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
; its title bar and close it with its close box, which a desk window has
; neither of until v2's wave 2 (SPEC.md 107.9).
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
DIA_BSS     equ 16                  ; the probe's own counters, then the
                                    ; include's state (a paragraph each)
%else
DIA_BSS     equ 0
%endif

%ifdef DP_DESK
; --- the face (SPEC.md 107.11's shape, in miniature) --------------------------
DPF_W       equ 128                 ; the face: two cards, 128 wide
DPF_C1      equ 40                  ; card 1, rows 0..39
DPF_GAP     equ 8                   ; then a gap of ground
DPF_C2      equ 40                  ; card 2
DPF_H       equ DPF_C1 + DPF_GAP + DPF_C2 + DPF_GAP     ; and a gap under it
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
    mov cx, DPF_GAP
    call dp_gap
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

dp_click:                       ; SI = the window: count it, nothing else
    inc byte [dp_clicks]
    ret

dp_up:
    inc byte [dp_ups]
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
