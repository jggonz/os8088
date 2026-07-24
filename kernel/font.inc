; =============================================================================
; font.inc - 8x8 text for mode 12h (SPEC.md 6)
;
; No font bytes are hard-coded: font_init asks the VGA BIOS for its ROM 8x8
; character set (int 10h AX=1130h BH=03h) and copies the printable range,
; glyphs 32..126, into a kernel buffer. Drawing uses Set/Reset with the Bit
; Mask so the background stays untouched (transparent): the glyph row is
; widened to a 16-bit window and shifted right by x&7, giving the masks for
; the one or two VRAM bytes the row lands on. A read of each byte loads the
; latches, so unmasked pixels are written back unchanged.
; =============================================================================

FONT_FIRST  equ 32              ; first glyph kept from the ROM set
FONT_LAST   equ 126             ; last  glyph kept
FONT_BYTES  equ (FONT_LAST - FONT_FIRST + 1) * 8    ; 95 glyphs x 8 rows = 760

; -----------------------------------------------------------------------------
; font_init - copy the ROM 8x8 font's printable range into RAM
;
; in:       nothing (must run after vga_mode12; uses int 10h)
; out:      nothing
; clobbers: nothing
; -----------------------------------------------------------------------------
font_init:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es

    mov ax, 0x1130              ; get font information
    mov bh, 0x03                ; pointer wanted: ROM 8x8 character set
    int 0x10                    ; -> ES:BP = font table

    mov si, bp
    add si, FONT_FIRST * 8      ; skip the control-character glyphs
    mov di, font_glyphs
    mov cx, FONT_BYTES / 2
    mov dx, es                  ; ROM segment becomes the source
    mov ax, KERNEL_SEG
    mov es, ax                  ; destination: our .bss buffer
    mov ds, dx
    cld
    rep movsw

    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_char - draw one 8x8 glyph, transparent background
;
; in:       CX = x, DX = y (absolute screen px), AL = character,
;           color from [gfx_color]; caller holds the gfx lock
; out:      nothing
; clobbers: nothing
;
; Characters outside 32..126 draw nothing (the caller's pen still advances).
; Clipping is whole-char: if any part of the 8x8 cell would leave the screen
; the glyph is skipped entirely (acceptable v1 behavior per SPEC.md 6).
; -----------------------------------------------------------------------------
font_char:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es

    cmp al, FONT_FIRST          ; unprintable -> space -> nothing to draw
    jb .done
    cmp al, FONT_LAST
    ja .done
    cmp cx, SCREEN_W - 8        ; unsigned: negative x fails this too
    ja .done
    cmp dx, SCREEN_H - 8
    ja .done

    sub al, FONT_FIRST          ; SI = glyph row data
    mov ah, 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, font_glyphs

    mov bx, cx                  ; BX = x; free CX for the shift count
    mov ax, ROW_BYTES
    mul dx                      ; AX = y * 80 (DX clobbered, product fits)
    mov di, ax
    mov ax, bx
    shr ax, 1
    shr ax, 1
    shr ax, 1
    add di, ax                  ; DI = y * 80 + x / 8
    mov cl, bl
    and cl, 7                   ; CL = x & 7, right shift for the 16-bit window

    mov ax, VGA_SEG
    mov es, ax

    mov dx, 0x3CE               ; GC index port
    mov ah, [gfx_color]
    mov al, 0
    out dx, ax                  ; GC0 Set/Reset = color
    mov ax, 0x0F01
    out dx, ax                  ; GC1 enable Set/Reset on all planes

    mov ch, 8                   ; row counter
.row:
    mov ah, [si]                ; glyph row in the high byte of the window
    inc si
    xor al, al
    shr ax, cl                  ; AH = first-byte mask, AL = second-byte mask
    mov bx, ax                  ; keep both masks in BH/BL

    or bh, bh                   ; anything landing in the first byte?
    jz .no_first
    mov al, 8
    mov ah, bh
    out dx, ax                  ; GC8 Bit Mask = first-byte glyph bits
    mov al, [es:di]             ; load latches
    mov [es:di], al             ; masked pixels <- Set/Reset color
.no_first:
    or bl, bl                   ; spill into the second byte?
    jz .no_second
    mov al, 8
    mov ah, bl
    out dx, ax                  ; GC8 Bit Mask = second-byte glyph bits
    mov al, [es:di+1]           ; load latches
    mov [es:di+1], al
.no_second:
    add di, ROW_BYTES
    dec ch
    jnz .row

    mov ax, 0x0001
    out dx, ax                  ; GC1 enable Set/Reset = 0 (default, SPEC 1.7)
    mov ax, 0xFF08
    out dx, ax                  ; GC8 Bit Mask = FF (default)
.done:
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_str - draw a NUL-terminated string left to right
;
; in:       CX = x, DX = y, SI -> ASCIIZ string, color from [gfx_color];
;           caller holds the gfx lock
; out:      nothing
; clobbers: nothing
; -----------------------------------------------------------------------------
font_str:
    push ax
    push cx
    push si
.next:
    lodsb
    or al, al
    jz .done
    call font_char              ; out-of-range chars draw nothing; pen still
    add cx, 8                   ; advances, so they read as spaces
    jmp .next
.done:
    pop si
    pop cx
    pop ax
    ret

; -----------------------------------------------------------------------------
; font_width - pixel width of a NUL-terminated string
;
; in:       SI -> ASCIIZ string
; out:      AX = 8 * length
; clobbers: nothing else
; -----------------------------------------------------------------------------
font_width:
    push si
    xor ax, ax
.next:
    cmp byte [si], 0
    je .done
    inc si
    add ax, 8
    jmp .next
.done:
    pop si
    ret

; -----------------------------------------------------------------------------
section .bss
font_glyphs:    resb 760        ; glyphs 32..126, 8 bytes per row (FONT_BYTES)
section .text
