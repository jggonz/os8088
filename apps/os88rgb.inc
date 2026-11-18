; =============================================================================
; os8088 - apps/os88rgb.inc
;
; An RGB triple to the nearest of os8088's sixteen colours, and the table of
; those sixteen. TWO READERS SHARE IT AS SOURCE: apps/os88img.inc's palette
; map (a BMP's or PCX's palette, SPEC.md 93) and the Browser's colour part
; (an HTML colour, SPEC.md 71.34) - one metric and one table, so a page and a
; picture never disagree about which sixteen-colour red #CC0000 is.
;
; The metric is |dR| + |dG| + |dB| on the 8-bit components, under 766, so no
; `mul`; a tie keeps the lower index.
;
; THE TABLE IS READ THROUGH CS: it is in the image of whatever includes this
; file, which for an overlay or a part is not the segment DS names.
;
; Section discipline: it opens nothing and leaves the assembler in whatever
; section its includer was in.
; =============================================================================

; rgb_near - AL = R, AH = G, CL = B -> AL = the nearest colour, 0..15.
; AH clobbered; every other register kept
rgb_near:
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov di, ax                      ; DI = R (low) and G (high)
    xor ch, ch
    mov si, cx                      ; SI = B
    mov bp, 0FFFFh                  ; BP = the best distance so far
    xor bx, bx                      ; BX = the candidate's triple
    xor dx, dx                      ; DL = the candidate, DH = the best
.cand:
    xor cx, cx
    mov ax, di
    call .d
    mov ax, di
    mov al, ah
    call .d
    mov ax, si
    call .d
    cmp cx, bp
    jae .next
    mov bp, cx
    mov dh, dl
.next:
    inc dl
    cmp dl, 16
    jb .cand
    mov al, dh
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    ret
.d:                                 ; CX += |AL - the table's|, BX on
    sub al, [cs:bx+rgb_pal8088]
    jnc .pos
    neg al
.pos:
    xor ah, ah
    add cx, ax
    inc bx
    ret

; os8088's own sixteen, CBLACK..CWHITE, as R,G,B: the table
; tools/os88pix.py matches on the host and apps/os88chart.inc writes into a
; BMP palette. R,G,B order because that is a PCX header's; a BMP's is
; byte-reversed where it is read.
rgb_pal8088: db 0x00,0x00,0x00,  0x00,0x00,0xAA,  0x00,0xAA,0x00,  0x00,0xAA,0xAA
    db 0xAA,0x00,0x00,  0xAA,0x00,0xAA,  0xAA,0x55,0x00,  0xAA,0xAA,0xAA
    db 0x55,0x55,0x55,  0x55,0x55,0xFF,  0x55,0xFF,0x55,  0x55,0xFF,0xFF
    db 0xFF,0x55,0x55,  0xFF,0x55,0xFF,  0xFF,0xFF,0x55,  0xFF,0xFF,0xFF
