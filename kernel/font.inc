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
; in:       nothing (must run after vid_init; uses int 10h)
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

    xor ax, ax                  ; zero the answer first: on a pre-EGA BIOS
    mov es, ax                  ; AH=11h is not implemented and returns with
    xor bp, bp                  ; ES:BP untouched
    mov ax, 0x1130              ; get font information
    mov bh, 0x03                ; pointer wanted: ROM 8x8 character set
    int 0x10                    ; -> ES:BP = font table
    mov ax, es
    or ax, bp
    jnz .have
    mov ax, 0xF000              ; the IBM PC/XT ROM's own 8x8 set, which is
    mov es, ax                  ; where a CGA or Hercules machine gets it
    mov bp, 0xFA6E
.have:

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
; the glyph is skipped entirely (acceptable v1 behavior per SPEC.md 6) - and,
; while a background task's clip region is armed, if any part of the cell
; would leave THAT (SPEC.md 11.3). A glyph is one shape or none; the region
; is the only thing here that could produce half of one, and half an 8x8
; glyph is unreadable anyway.
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
    cmp cx, [vid_wm8]           ; unsigned: negative x fails this too
    ja .done
    cmp dx, [vid_hm8]
    ja .done
    cmp word [wm_clip_n], 0     ; the cell whole against the clip region too
    je .noclip                  ; (SPEC.md 11.3)
    push ax
    mov ax, cx
    mov bx, dx
    mov cx, ax
    add cx, 7
    mov dx, bx
    add dx, 7
    call wm_clip_test
    mov cx, ax                  ; MOV and POP leave CF alone
    mov dx, bx
    pop ax
    jc .done
.noclip:

    sub al, FONT_FIRST          ; SI = glyph row data
    mov ah, 0
    mov si, ax
    shl si, 1
    shl si, 1
    shl si, 1
    add si, font_glyphs

    call bb_mono_chk            ; both modes - see gfx_fill (SPEC.md 32).
                                ; Placed after the clip so a glyph that draws
                                ; nothing cannot retire the flag.
    cmp byte [bb_on], 0         ; double-buffered? (SPEC.md 32)
    je .vram
    call font_char_bb
    jmp .done
.vram:
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
; font_char_bb - back-buffer glyph renderer (SPEC.md 32)
;
; The same shifted-row masks as the VRAM path, applied to all four planes
; in software: the [gfx_color] plane bit picks or (set bits) or and-not
; (clear bits). When x is byte-aligned the second-byte mask is always 0
; and that write is skipped, so nothing lands past the row's last byte.
; in:       CX = x, DX = y (cell fully on screen - font_char clipped),
;           SI -> 8 glyph row bytes
; clobbers: ax, bx, cx, dx, si, di, es (font_char's epilogue restores all)
;
; **This is the mono adapters' ONLY text renderer** (SPEC.md 39.5), so its
; row loop is the innermost loop of every string os8088 draws on a Hercules
; or CGA machine - and those are the slow machines. Three things it does that
; the straight port did not, all of them the VRAM path's own tricks arriving
; late:
;
; - **The ink test is hoisted.** [gfx_color]'s plane bit cannot change inside
;   a plane, so `test ch, 1` belongs above the eight rows rather than in them.
;   Two loops instead of one branch per row.
; - **A blank glyph row is skipped whole.** or-ing in 0 and and-ing in FF are
;   both identity, and a read-modify-write of framebuffer memory is ~30 cycles
;   on an 8088 whether or not it changes anything. Most glyphs have a blank
;   descender row and many a blank top row; a SPACE is eight of them, and the
;   Task Manager's columns are largely spaces.
; - **gfx_nextrow is inlined.** Its body is three instructions and the call
;   and ret around them cost as much again - about a third of a row's work
;   once the redundant writes above are gone. The CS overrides come with it:
;   see gfx_nextrow for why they are not optional.
; -----------------------------------------------------------------------------
font_char_bb:
    mov ax, cx                  ; dirty: columns x/8..(x+7)/8, rows y..y+7
    mov bx, cx
    add bx, 7
    mov di, cx
    and di, 7                   ; DI parks the shift (x & 7) for a moment
    mov cl, 3
    shr ax, cl
    shr bx, cl                  ; same byte column when x & 7 = 0
    push di
    push dx
    mov cx, dx
    add dx, 7
    push ax                     ; left column survives bb_dirty for the
    call bb_dirty               ; offset math below
    pop bx
    pop dx
    mov ax, dx                  ; DI = rowbase(y) + x / 8, CL = x & 7
    call gfx_rowbase            ; (CX is popped back below, DX is dead)
    add ax, bx
    mov di, ax
    pop cx                      ; CL = right shift for the 16-bit window

    mov [font_bb_si], si        ; per-plane restart points
    mov [font_bb_di], di
    mov dx, [vid_rseg]          ; DX = plane segment - the framebuffer on
    mov ch, [gfx_color]         ; mono (SPEC.md 39.3)
    call font_ink               ; CH = colour bits, shifted right per plane;
                                ; on mono, 00 or FF so `test ch,1` still works
    mov bl, [vid_planes]        ; BL = plane counter: 4 or 1
.plane:
    mov es, dx
    mov si, [font_bb_si]
    mov di, [font_bb_di]
    mov bh, 8                   ; row counter
    test ch, 1                  ; this plane's ink, decided ONCE rather than
    jz .crow                    ; once per row
.srow:
    mov ah, [si]                ; glyph row in the window's high byte
    inc si
    or ah, ah                   ; a blank row ors in nothing, whatever the
    jz .snext                   ; shift does to it
    xor al, al
    shr ax, cl                  ; AH = first-byte mask, AL = second-byte mask
    or [es:di], ah
    or al, al                   ; ...and nothing spilled into the second byte
    jz .snext                   ; at all when x is byte-aligned
    or [es:di+1], al
.snext:
    add di, [cs:vid_rowadd]     ; gfx_nextrow, inlined (see the note above)
    test di, [cs:vid_wrapbit]
    jz .snw
    add di, [cs:vid_wrapfix]
.snw:
    dec bh
    jnz .srow
    jmp short .pnext
.crow:
    mov ah, [si]
    inc si
    or ah, ah                   ; a blank row masks nothing out either
    jz .cnext
    xor al, al
    shr ax, cl
    not ah
    and [es:di], ah
    or al, al
    jz .cnext
    not al
    and [es:di+1], al
.cnext:
    add di, [cs:vid_rowadd]
    test di, [cs:vid_wrapbit]
    jz .cnw
    add di, [cs:vid_wrapfix]
.cnw:
    dec bh
    jnz .crow
.pnext:
    add dx, [vid_rpara]
    shr ch, 1
    dec bl
    jnz .plane
    ret

; -----------------------------------------------------------------------------
; font_ink - internal: font_char_bb's per-plane colour register
;
; The glyph loop tests bit 0 of CH once per plane and shifts, so on VGA the
; colour index goes in untouched. On mono there is one plane and one bit to
; give it: gfx_ink's black or white. Glyphs never dither - a dithered 8x8
; glyph is unreadable - so the middle greys round to black, which is right
; for every string the UI draws (dark grey disabled text on white stays
; legible, and Piano's coloured letters keep their contrast).
; in:       CH = [gfx_color]
; out:      CH = the colour index (VGA) or 00/FF (mono)
; clobbers: flags
; -----------------------------------------------------------------------------
font_ink:
    cmp byte [vid_mono], 0
    je .out
    push ax
    mov al, ch
    call gfx_ink
    cmp al, 0xFF                ; only pure white lights a mono pixel
    je .white
    xor al, al
.white:
    mov ch, al
    pop ax
.out:
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
    push es                     ; the kernel's own strings live in DS; the
    push ds                     ; ES-taking body below is the one that draws
    pop es                      ; (SPEC.md 20.1: a package's are in its own
    call font_str_x             ; segment, and wm/menu pass that in ES)
    pop es
    ret

; -----------------------------------------------------------------------------
; font_str_x - font_str with the string at ES:SI
;
; in:       CX = x, DX = y, ES:SI -> ASCIIZ, [gfx_color] = pen
; out:      nothing
; clobbers: nothing else (flags)
;
; Since packages left the kernel segment (SPEC.md 20.1) a drawn string can
; belong to the kernel or to any loaded package, and the caller is the only
; one who knows which. Five call sites in the tree pass a package segment -
; the window title (wm_draw_win), the app name and menu titles
; (menu_draw_bar), and the item strings (menu_drop, menu_widest); every other
; caller reaches this through font_str above with ES = DS.
; -----------------------------------------------------------------------------
font_str_x:
    push ax
    push cx
    push si
    cld
.next:
    mov al, [es:si]
    inc si
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
    push es
    push ds
    pop es
    call font_width_x
    pop es
    ret

; -----------------------------------------------------------------------------
; font_width_x - font_width with the string at ES:SI (SPEC.md 20.1)
; in:       ES:SI -> ASCIIZ string
; out:      AX = 8 * length
; clobbers: nothing else
; -----------------------------------------------------------------------------
font_width_x:
    push si
    xor ax, ax
.next:
    cmp byte [es:si], 0
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
font_bb_si:     resw 1          ; bb path: glyph pointer, reloaded per plane
font_bb_di:     resw 1          ; bb path: cell offset, reloaded per plane
section .text
