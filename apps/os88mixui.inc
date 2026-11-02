; The mixer family's mechanical slider, SPEC.md 34.15. Shared SOURCE by
; SOUND.DRV and MIXER.O88. A complete band is composed in RAM and written
; once: no erase/ink flash, including the percentage and engraved scale.
; mixui_draw: AX/BX=screen x/y, CL=0..100, SI=label. Gfx lock held.
; Clobbers AX/BX/CX/DX/SI/DI/BP, preserves ES. Caller supplies MIXUI_BUFFER.
%define GFXE_TEXT
%define GFXE_NOBOLD
%ifndef MIXUI_H                 ; the band's rows: 48, or as few as 44 - the
  %define MIXUI_H 48            ; last thing drawn ends at row 43 (the long
%endif                          ; scale marks), so rows 44..47 are always the
%if MIXUI_H < 44                ; cleared ground. An includer whose ground
  %error "os88mixui.inc: the scale marks end at row 43"  ; under them is
%endif                          ; already that white may stop at 44 (SOUND.DRV)
%define GFXE_BAND_W 176
%define GFXE_BAND_H MIXUI_H
%define GFXE_BAND_BUF MIXUI_BUFFER
%ifdef MIXUI_BAND_SEG           ; ...an offset in the segment this word holds
  %define GFXE_BAND_SEG MIXUI_BAND_SEG  ; rather than in DS (os88gfx.inc's
%endif                          ; GFXE_BAND_SEG; SOUND.DRV's popup claim)
%include "os88gfx.inc"
%define DEC_U16
%include "os88dec.inc"

; A tiny rectangle in the RAM band (zero=black ink, INV=white).
%macro MX_RECT 5
    mov byte [gfxe_tsty], %5
    mov ax, %1
    mov bx, %2
    mov cx, %3
    mov dx, %4
    call gfxe_bfill
%endmacro

mixui_draw:
    push es
    mov [mixui_x], ax
    mov [mixui_y], bx
    xor ch, ch
    mov [mixui_value], cx
    push si
    mov word [gfxe_bh], MIXUI_H
    mov ax, 0FFFFh
    call gfxe_bclear
    mov byte [gfxe_tsty], 0
    pop si
    xor cx, cx
    xor dx, dx
    call gfxe_text
    mov ax, [mixui_value]
    mov di, mixui_pct
    call dec_u16
    mov byte [di], '%'
    mov byte [di+1], 0
    mov si, mixui_pct
    mov cx, 136
    xor dx, dx
    call gfxe_text
    ; Recessed double rail. The white center leaves a clean, crisp slot on
    ; monochrome cards. Small scale marks sit below, clear of the thumb.
    ; The ten fixed ones are a TABLE (mixui_rects): five bytes a rectangle
    ; where MX_RECT's five stores and a call were twenty.
    mov si, mixui_rects
.rect:
    lodsb
    mov [gfxe_tsty], al         ; the style...
    xor ax, ax
    mov bx, ax
    mov cx, ax
    mov dx, ax
    lodsb
    mov dl, al                  ; ...h, w, y, then x into AX
    lodsb
    mov cl, al
    lodsb
    mov bl, al
    lodsb
    push si
    call gfxe_bfill             ; (which spends SI)
    pop si
    cmp si, mixui_rects_end
    jb .rect
    ; 12 + percent*150/100 is the thumb CENTER, not its left edge.
    mov ax, [mixui_value]
    mov bx, 150
    mul bx
    mov bx, 100
    div bx
    add ax, 8
    mov [mixui_thumb], ax
    MX_RECT [mixui_thumb], 17, 9, 19, 0
    inc word [mixui_thumb]
    MX_RECT [mixui_thumb], 18, 7, 17, GFXE_INV
    inc word [mixui_thumb]
    MX_RECT [mixui_thumb], 21, 1, 11, 0
    add word [mixui_thumb], 2
    MX_RECT [mixui_thumb], 21, 1, 11, 0
    add word [mixui_thumb], 2
    MX_RECT [mixui_thumb], 21, 1, 11, 0
    push ds
    pop es
    mov al, CWHITE
    mov ah, CBLACK
    call OSAPI_GFX_BLIT1_PEN
%ifdef MIXUI_BAND_SEG
    mov es, [MIXUI_BAND_SEG]    ; the band's segment for the blit
%endif
    mov si, MIXUI_BUFFER
    mov bp, 22
    mov ax, [mixui_x]
    mov bx, [mixui_y]
    mov cx, 176
    mov dx, MIXUI_H
    call OSAPI_GFX_BLIT1
    pop es
    ret

; mixui_draw's fixed rectangles, drawn in this order: style, h, w, y, x
%macro MX_ROW 5                 ; x, y, w, h, style - MX_RECT's order
    db %5, %4, %3, %2, %1
%endmacro
mixui_rects:
    MX_ROW 12, 24, 152, 4, 0
    MX_ROW 13, 26, 150, 1, GFXE_INV
    MX_ROW 12, 40, 1, 4, 0
    MX_ROW 49, 40, 1, 2, 0
    MX_ROW 87, 40, 1, 4, 0
    MX_ROW 124, 40, 1, 2, 0
    MX_ROW 162, 40, 1, 4, 0
    MX_ROW 0, 25, 6, 1, 0
    MX_ROW 169, 25, 7, 1, 0
    MX_ROW 172, 22, 1, 7, 0
mixui_rects_end:

; Pointer to percentage. CX=screen x, AX=band's left. Values clamp outside
; the track; the center mapping is exactly the painter's inverse.
mixui_hit:
    sub cx, ax
    sub cx, 12
    jns .lo
    xor cx, cx
.lo:
    cmp cx, 150
    jbe .hi
    mov cx, 150
.hi:
    mov ax, cx
    mov cx, 100
    mul cx
    add ax, 75
    mov cx, 150
    div cx
    ret

; Scale a saved stereo pair to a new peak, preserving its ratio. AL=new
; peak, CX=baseline L/R -> CL/CH=new L/R. A silent baseline restarts centered.
mixui_scale:
    mov dl, cl
    cmp dl, ch
    jae .max
    mov dl, ch
.max:
    or dl, dl
    jnz .scale
    mov cl, al
    mov ch, al
    ret
.scale:
    mov dh, al
    mul cl
    div dl
    mov cl, al
    mov al, dh
    mul ch
    div dl
    mov ch, al
    ret
mixui_x:     dw 0
mixui_y:     dw 0
mixui_value: dw 0
mixui_thumb: dw 0
mixui_pct:   times 5 db 0
