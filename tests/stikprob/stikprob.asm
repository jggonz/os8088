; =============================================================================
; tests/stikprob/stikprob.asm - the smallest STICKY APP
; (SPEC.md 110)
;
; Not shipped software: the gate package `make stiktest` puts in STIKAPPS\
; of its fixture system disks. It is apps/os88stik.inc and a window, nothing
; else, so every row that boots it asks about the MECHANISM and not about an
; application:
;
;   stikboot   SYSTEM.CFG's Sticky App row launches it at boot, where STIKPROB.DAT
;              says its window was, and it holds no desktop cell
;   stikmiss    a row beside a missing one still launches
;   stikhiber  ...and is NOT launched under a pending resume question
;
; It registers its row on its first tick (a byte-identical row is a no-op
; that writes nothing) and removes it BY NAME on close. Its payload is EMPTY:
; the Sticky App header alone is its state, STIKPROB.DAT is 16 bytes.
;
; **THE DESK BUILD, `-DDP_DESK`** (SPEC.md 11.103, v2's wave 1) is the same
; source as DESKPROB.O88, 'DeskProbe', DESKPROB.DAT: header flag OS88_F_DESK,
; OSAPI_WM_DESK's QUERY and MAKE in the entry proc, and a face of TWO WHITE
; CARDS with the desktop's own ground between them, below them and down the
; frame's left column, through OSAPI_GFX_DESK - every pixel once. It counts
; its presses, releases and keys in its bss for desk*.py to read, and gives
; the bar back (RELEASE) on Esc. Where QUERY refuses (kern_small) it is the
; TITLED FALLBACK: the same face under an ordinary title bar (SPEC.md 110.7).
; It is its own build, not STIKPROB's, because the v1 rows drag StikProbe by
; its title bar and close it with its close box (SPEC.md 110.9).
;
; WAVE 2 (SPEC.md 11.103.6-7) gives it a crude TAG ROW under the cards: at
; rest a white plate on the desktop's ground; while the kernel's hover edge
; says WF_HOVER (its W_ONWAKE reads the bit, and refuses to REVEAL with a
; button held) a black HANDLE with a white close box at its left. A press on
; anything that is not a card - a gap, the tag, a ground column - posts
; OSAPI_WM_DESK DRAG; a press on the revealed box arms it (lit), and its
; release ON the box closes the probe through the include's own negotiator
; and OSAPI_WM_CLOSE, as a widget's x must (SPEC.md 110.11). Its STIK_ONTICK
; posts a DRAG from the TIMER on request, which must be inert.
;
; WAVE 3 (SPEC.md 110.11) MOVES IT ONTO THE INCLUDE: `%define STIK_WIDGET`,
; and every pixel, gesture and timer of the face is apps/os88stik.inc's -
; the probe is three layouts (TALL 104 rows, MID 83, COMPACT 65, picked by
; the screen's height), a STIK_COMPOSE that exercises GFXE_TEXT and
; GFXE_BITS on its cards (a label, a x3 pair, bold, a box with white text in
; it, a tick, a x2 bold glyph off the byte grid) through a 16-row band so a
; glyph straddles two slabs, and counters: its card presses and releases
; (STIK_ONCARD / STIK_ONCARDUP), its wakes (STIK_WAKE, tail-jumping to
; stik_wake), its focus changes (STIK_ONFOCUS) and its paints.
; =============================================================================

%include "os88api.inc"

%ifdef DP_DESK
    OS88_HEADER 'DeskProbe', dp_entry, OS88_F_ICON | OS88_F_DESK
%else
    OS88_HEADER 'StikProbe', dp_entry, OS88_F_ICON
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
%define STIK_DATNAME 'DESKPROB.DAT'
%define STIK_CAP    'DeskProbe'
%else
%define STIK_DATNAME 'STIKPROB.DAT'
%define STIK_CAP    'StikProbe'
%endif
STIK_PAYLEN equ 0
STIK_PAYVER equ 1
STIK_ICON   equ 32
%ifdef DP_DESK
STIK_BSS    equ 288                 ; the probe's own counters (32), its band
                                    ; (256), then the include's (paragraphs)
%else
STIK_BSS    equ 0
%endif

%ifdef DP_DESK
; --- the widget (SPEC.md 110.11), in miniature -------------------------------
%define STIK_WIDGET
%define STIK_TAG      'DeskProbe'   ; = the title
%define STIK_FACE_W   128
%define STIK_LAYOUTS  dp_lays
%define STIK_PAINTCARD stik_slabs    ; the include's slab painter...
%define STIK_COMPOSE  dp_compose    ; ...and what it composes with
%define STIK_ONCARD   dp_card
%define STIK_ONCARDUP dp_cardup
%define STIK_ONFOCUS  dp_focus
%define STIK_WAKE     dp_wake       ; counted, then the include's
%define STIK_ONTICK   dp_ontick
%define GFXE_TEXT
%define GFXE_BITS
%define GFXE_BAND_W   128
%define GFXE_BAND_H   16            ; 16 rows: a 40-row card is three slabs,
%define GFXE_BAND_BUF dp_band       ; and the x3 pair straddles two of them

; the probe's bss, read by tests/desk*.py at image + these offsets
dp_clicks   equ os88_image_end + 0  ; byte: card presses (STIK_ONCARD)
dp_ups      equ os88_image_end + 1  ; byte: card releases (STIK_ONCARDUP)
dp_keys     equ os88_image_end + 2  ; byte: W_ONKEYs taken (Esc included)
dp_desk     equ os88_image_end + 3  ; byte: 1 = MAKE said yes, a desk window
dp_lastkey  equ os88_image_end + 4  ; word: the last key's AX
dp_paints   equ os88_image_end + 6  ; word: W_PAINTs
dp_rel      equ os88_image_end + 8  ; byte: RELEASE's CF on the last Esc + 1
dp_wakes    equ os88_image_end + 9  ; byte: W_ONWAKEs taken (the hover edge)
dp_tdrag    equ os88_image_end + 13 ; byte: 1 = post a DRAG from the TIMER
                                    ; (the test sets it), 2 = it was posted;
                                    ; 3 = lay OSAPI_GFX_DESK over the face's
                                    ; top 4 rows from the beat, 4 = laid -
                                    ; the cell's one runtime caller on
                                    ; kern_small, whose titled gaps are white
dp_ondrags  equ os88_image_end + 16 ; byte: W_ONDRAGs taken - a posted drag
                                    ; must deliver NONE (it arms nothing)
dp_foci     equ os88_image_end + 17 ; byte: STIK_ONFOCUS calls
dp_focl     equ os88_image_end + 18 ; byte: ...the last one's AL
dp_lcard    equ os88_image_end + 19 ; byte: the last card pressed
dp_band     equ os88_image_end + 32 ; 256: the composer's band, 128 x 16
%endif

dp_entry:
    push si
    push di
    mov di, dp_tpl
    call stik_load              ; SI = the loader's name; places the template
    jc .out                     ; ...or another instance is up: say no
    mov si, dp_tpl
    call OSAPI_WM_CREATE
    jc .out
%ifdef DP_DESK
    mov ax, dp_ondrag           ; counted: an armed card press may deliver
    call OSAPI_WM_ONDRAG        ; some, a posted DRAG none (CF=1 on small)
%endif
    call stik_made
%ifdef DP_DESK
    test byte [stik_wf], STIK_W_DESK
    jz .t
    mov byte [dp_desk], 1       ; MAKE took
.t:
%endif
    clc
.out:
    pop di
    pop si
    ret

%ifdef DP_DESK
dp_paint:                       ; W_PAINT: counted, then the include's
    inc word [dp_paints]
    jmp stik_paint

dp_card:                        ; STIK_ONCARD: AL = the card, CX,DX in it
    inc byte [dp_clicks]
    mov [dp_lcard], al
    ret

dp_cardup:                      ; STIK_ONCARDUP
    inc byte [dp_ups]
    ret

dp_focus:                       ; STIK_ONFOCUS: AL = 1 gained, 0 lost
    inc byte [dp_foci]
    mov [dp_focl], al
    ret

dp_wake:                        ; STIK_WAKE: counted, then the include's
    inc byte [dp_wakes]
    jmp stik_wake

dp_ondrag:                      ; W_ONDRAG: counted, nothing else
    inc byte [dp_ondrags]
    ret

; dp_compose - STIK_COMPOSE: AL = the card, BX = the slab's first card row,
; CX = its rows; compose at y = card row - BX. Card 0 a label; card 1 a
; sample of everything GFXE_TEXT and GFXE_BITS do, which tests/desklayer.py's
; deskground leg composes again on the host from the guest's own font
dp_compose:
    mov [dp_r0], bx
    or al, al
    jnz .c1
    mov byte [gfxe_tsty], 0             ; card 0: black on white
    mov si, dp_lab
    mov cx, 8
    mov dx, 8
    call dp_text
    mov si, dp_sq                       ; x1 off the grid in the stride's
    mov cx, 113                         ; last two bytes, across a slab: the
    mov dx, 12                          ; straight path clipped head and foot
    call dp_text
    mov byte [gfxe_tsty], GFXE_BOLD     ; ...and bold in the LAST byte, cut
    mov si, dp_sw                       ; at the stride: the general path's x1
    mov cx, 122
    mov dx, 12
    call dp_text
    ret
.c1:
    cmp al, 1
    jne .r
    mov byte [gfxe_tsty], GFXE_X3       ; a x3 pair, straddling two slabs
    mov si, dp_s24
    mov cx, 4
    mov dx, 6
    call dp_text
    mov byte [gfxe_tsty], GFXE_BOLD     ; bold, off the byte grid
    mov si, dp_smo
    mov cx, 60
    mov dx, 6
    call dp_text
    mov byte [gfxe_tsty], 0             ; a black box...
    mov ax, 58
    mov bx, 18
    sub bx, [dp_r0]
    mov cx, 36
    mov dx, 10
    call gfxe_bfill
    mov byte [gfxe_tsty], GFXE_INV      ; ...with white text in it
    mov si, dp_saug
    mov cx, 60
    mov dx, 19
    call dp_text
    mov byte [gfxe_tsty], 0             ; a tick
    mov si, dp_tick
    mov cx, 100
    mov dx, 8
    sub dx, [dp_r0]
    call gfxe_bits
    mov byte [gfxe_tsty], GFXE_X2 | GFXE_BOLD   ; x2 bold at an odd x
    mov si, dp_s7
    mov cx, 101
    mov dx, 22
    call dp_text
.r:
    ret

dp_text:                        ; gfxe_text at card row DX
    sub dx, [dp_r0]
    jmp gfxe_text

; dp_ontick - STIK_ONTICK, the beat (the timer, lock held): a DRAG posted from
; HERE when the test asks, which the next press must not turn into a drag -
; or the desktop's ground laid over the face's top rows (no clip is armed
; here: desksmall asks with the probe alone on the glass)
dp_ontick:
    cmp byte [dp_tdrag], 3
    jne .d
    push ax
    push bx
    push cx
    push dx
    mov ax, [stik_fx]
    mov bx, [stik_fy]
    mov cx, ax
    add cx, STIK_FACE_W - 1
    mov dx, bx
    add dx, 3
    call OSAPI_GFX_DESK
    mov byte [dp_tdrag], 4
    pop dx
    pop cx
    pop bx
    pop ax
    jmp short .r
.d:
    cmp byte [dp_tdrag], 1
    jne .r
    push ax
    push bx
    mov bx, [stik_win]
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
    dw 40, 60, 0, 0             ; the TEMPLATE place: a valid state file
    dw dp_ttl, dp_paint, dp_key, 0  ; moves it; stik_load sizes it

dp_ttl:     db 'DeskProbe', 0
dp_lab:     db 'DeskProbe', 0
dp_s24:     db '24', 0
dp_smo:     db 'Mo', 0
dp_saug:    db 'AUG', 0
dp_s7:      db '7', 0
dp_sq:      db 'Q', 0
dp_sw:      db 'W', 0
dp_tick:    db 8                ; a 2-px tick, 12 wide
            dw 00030h, 00060h, 000C0h, 0C180h, 06300h, 03600h, 01C00h, 00800h
dp_r0:      dw 0                ; dp_compose's slab row
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
    call stik_painted           ; kern_small: hire the worker (SPEC.md 110.5.2)
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

dp_tpl:
    dw 40, 60, 160, 50          ; the TEMPLATE place: a valid state file
    dw dp_ttl, dp_paint, 0, 0   ; moves it

dp_ttl:     db 'StikProbe', 0
dp_msg:     db 'Sticky App probe', 0
%endif

%ifdef DP_DESK
%include "os88gfx.inc"
%endif
%include "os88stik.inc"
%ifdef DP_DESK
; three layouts, picked by the screen's height (SPEC.md 110.11) - after the
; include, which is where STIK_LAYOUT is defined
dp_lays:
    STIK_LAYOUT 436, 60, 12, 40,8, 40,4      ; TALL, VGA: 104 rows
    STIK_LAYOUT 304, 40, 11, 32,4, 32,4      ; MID, Hercules and EGA: 83
    STIK_LAYOUT 156, 22, 11, 24,3, 24,3      ; COMPACT, CGA: 65
%endif

    align 16, db 0              ; os88stik.inc's read lands on a paragraph
    OS88_BSS STIK_BSS_END
    OS88_IMAGE_END
