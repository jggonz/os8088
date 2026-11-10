; =============================================================================
; os8088 - tests/vgaprobe/vgaprobe.asm
;
; VGAPROBE: a FIELD INSTRUMENT for RealDMG's scrolling research (2026-10-21,
; docs/REALDMG-SPEC.md 15.3). NEVER shipped - `make vgaprobe` builds it onto a
; scratch floppy, build/vgaprobe.img. It asks a real VGA three things an
; emulator cannot answer for it:
;
;  1. WHEN does the card take a new START ADDRESS (CR0C/CR0D) and a new PEL
;     PANNING (AR13), relative to vertical sync? Commit a2af224e's hardware
;     scroll was refused for the ~7 ms it waited for the retrace before a
;     flip; 86Box's PVGA1A takes the start address at vsync start and the
;     panning at the next frame's top, so a pair written ANYWHERE but between
;     those two lands on one frame. Is the real chip the same, or does it
;     sample the panning on every line (EGA's way)?
;
;     THE SWEEP: two pages hold one picture of vertical lines, the second
;     shifted 4 pixels, so (page A, pan 4) and (page B, pan 0) SHOW THE SAME
;     IMAGE. Once a frame, at a chosen PHASE after vsync start, the probe
;     writes the other pair. Taken together the picture stands perfectly
;     still; taken a frame apart it SHIMMERS (lines doubled, 4px apart); a
;     panning sampled per line SHIMMERS ONLY BELOW A ROW - a tear that moves
;     with the phase. Walk the phase across the frame and write down where it
;     is still, where it shimmers, and whether the shimmer is whole or torn.
;
;  2. Does the CRTC's address WRAP at 64KB in mode 0Dh (W)? Keen's floating
;     pages need it; some SVGAs do not wrap. Stripes are drawn at F000-FFFF
;     and checks at 0280-1FFF, and the start address set to F000: stripes,
;     then the status text, then checks, means it wraps.
;
;  3. The frame's TIMING, by the PIT (T, and at every change): the frame,
;     the vsync's length, vsync start to the first displayed line, and the
;     displayed scanlines. R swaps in RealDMG's own vertical timing
;     (rwfsx.inc's rw_fscrv - the horizontal half is left stock, so the
;     probe's picture keeps its width) and measures again.
;
; THE STATUS is at the bottom of the screen, below a LINE COMPARE split with
; AR10's pixel panning mode set, so it neither moves nor pans; it shows
; VRAM 0.
;
; S cycles the status's split: the line compare with AR10's pixel panning
; mode (as first shipped), the split without it, and no split at all, the
; status then drawn into both pages (page A's 4 pixels right). The first
; field run (the Victory's PVGA1A, 2026-10-21) found mode 0 STILL only from
; the split's line to vsync: the panning is reloaded at the split, so the
; other two modes are what RealDMG's own screen - which has no split - is.
;
; Keys in the bracket: Left/Right the phase one scanline, Up/Down ten,
; PgUp/PgDn a hundred, Home to 0; Space toggling on/off (off holds page A);
; W the wrap test (W again leaves it); R RealDMG's vertical timing; T
; measure again; Esc back to the desktop, where the window keeps the last
; numbers.
; =============================================================================

%include "os88api.inc"

    OS88_HEADER 'VGAPROBE', vp_entry

VP_CONT_W   equ 404
VP_CONT_H   equ 92
VP_PAGEA    equ 2000h               ; the two pictures' VRAM offsets (bytes)
VP_PAGEB    equ 4000h
VP_LINE     equ 38                  ; PIT counts in a 31.78 us scanline
VP_WRAPS    equ 0F000h              ; the wrap test's start address

; -----------------------------------------------------------------------------
vp_entry:
    push si
    xor bx, bx
    call OSAPI_FSX_CAPS             ; AX = the modes this display offers
    mov [vp_caps], ax
    mov si, vp_tpl
    call OSAPI_WM_CREATE
    pop si
    ret

; vp_paint - W_PAINT: the legend and the last numbers
vp_paint:
    push si
    mov bx, si
    call OSAPI_WM_CONTENT
    mov [vp_wx], ax
    mov [vp_wy], dx
    mov si, vp_legend
    mov dx, 6
.l:
    lodsw
    or ax, ax
    jz .r
    push si
    mov si, ax
    mov cx, [vp_wx]
    add cx, 6
    push dx
    add dx, [vp_wy]
    mov ax, (CWHITE << 8) | CBLACK
    call OSAPI_FONT_RUN
    pop dx
    add dx, 10
    pop si
    jmp short .l
.r:
    pop si
    ret

; vp_onkey - W_ONKEY: Enter or P runs the probe
vp_onkey:
    push si
    cmp al, 13
    je .go
    or al, 20h
    cmp al, 'p'
    jne .r
.go:
    test word [vp_caps], 1 << FSXM_VGA0D
    jz .no
    call OSAPI_FONT_GLYPHS          ; DX:SI, AL = the first code: read in
    mov [vp_fontp], si              ;   place in there (RealDMG's way)
    mov [vp_fontp + 2], dx
    mov [vp_font0], al
    pop si                          ; (SI was the table: the window again)
    push si
    mov bx, si
    mov ax, vp_main
    xor cx, cx
    call OSAPI_FSX_RUN
    jnc .back
.no:
    mov si, vp_s_novga
    mov di, vp_s_res
    call vp_strcpy
.back:
    pop si
    push si
    mov bx, si
    call OSAPI_WM_CLIP_SET
    jc .r
    mov al, CWHITE
    call OSAPI_SET_COLOR
    mov ax, [vp_wx]
    mov bx, [vp_wy]
    mov cx, ax
    add cx, VP_CONT_W - 1
    mov dx, bx
    add dx, VP_CONT_H - 1
    call OSAPI_GFX_FILL
    pop si
    push si
    call vp_paint
.r:
    pop si
    ret

; =============================================================================
; vp_main - the bracket: DS = CS = ours, SI = the window, ES = KERNEL_SEG
; =============================================================================
vp_main:
    push ds
    pop es
    mov al, FSXM_VGA0D
    mov di, vp_info
    call OSAPI_FSX_MODE
    jnc .have
    mov si, vp_s_novga
    mov di, vp_s_res
    call vp_strcpy
    ret
.have:
    push ds
    pop es
    mov ax, [vp_info + FSI_SEG]
    mov [vp_seg], ax
    call vp_crtcsave
    call vp_pages
    mov byte [vp_rb], 0
    mov byte [vp_tog], 1
    mov byte [vp_wrap], 0
    call vp_split
    call vp_measure
    call vp_status
.frame:
    call vp_vsrise                  ; vsync starts now
    call pit_now
    mov [vp_t0], ax
    cmp byte [vp_wrap], 0
    jne .keys
.w:
    call pit_now                    ; ...and the phase after it
    sub ax, [vp_t0]
    cmp ax, [vp_phase]
    jb .w
    cmp byte [vp_tog], 0
    je .keys
    xor byte [vp_ab], 1
    call vp_pair
.keys:
    mov ah, 1
    int 16h
    jz .frame
    xor ah, ah
    int 16h
    cmp al, 27
    je .out
    call vp_key
    call vp_status
    jmp short .frame
.out:
    call vp_report                  ; the numbers into the window's lines
    ret

; vp_key - AX = a key, in the bracket
vp_key:
    mov bx, [vp_phase]
    cmp ah, 4Bh                     ; Left / Right: a scanline
    jne .k1
    sub bx, VP_LINE
.k1:
    cmp ah, 4Dh
    jne .k2
    add bx, VP_LINE
.k2:
    cmp ah, 48h                     ; Up / Down: ten
    jne .k3
    add bx, VP_LINE * 10
.k3:
    cmp ah, 50h
    jne .k4
    sub bx, VP_LINE * 10
.k4:
    cmp ah, 49h                     ; PgUp / PgDn: a hundred
    jne .k5
    add bx, VP_LINE * 100
.k5:
    cmp ah, 51h
    jne .k6
    sub bx, VP_LINE * 100
.k6:
    cmp ah, 47h                     ; Home
    jne .k7
    xor bx, bx
.k7:
    or bx, bx                       ; within the frame
    jns .k8
    xor bx, bx
.k8:
    mov cx, [vp_frame]
    sub cx, VP_LINE
    cmp bx, cx
    jb .k9
    mov bx, cx
.k9:
    mov [vp_phase], bx
    or al, al                       ; (an arrow's AL is 0 or E0h)
    jz .x
    cmp al, 0E0h
    je .x
    or al, 20h
    cmp al, ' '
    jne .t
    xor byte [vp_tog], 1
    mov byte [vp_ab], 1
    call vp_pair                    ; (off: page A held)
    ret
.t:
    cmp al, 't'
    jne .r
    jmp vp_measure
.r:
    cmp al, 's'
    je .sp
    cmp al, 'r'
    jne .wr
    xor byte [vp_rb], 1
    call vp_timing
    call vp_split
    jmp vp_measure
.sp:
    inc byte [vp_spl]               ; S: split + PPM, split, no split
    cmp byte [vp_spl], 3
    jb .sp1
    mov byte [vp_spl], 0
.sp1:
    call vp_pages                   ; (the pages' status rows clean again)
    call vp_split
    jmp vp_measure
.wr:
    cmp al, 'w'
    jne .x
    xor byte [vp_wrap], 1
    jz .wo
    call vp_wrapdraw
    mov bx, VP_WRAPS
    xor al, al
    jmp vp_setpair
.wo:
    mov byte [vp_ab], 1
    jmp vp_pair
.x:
    ret

; vp_pair - the pair [vp_ab] names: A = page A, pan 4; B = page B, pan 0.
; They show the same image
vp_pair:
    mov bx, VP_PAGEB
    xor al, al
    cmp byte [vp_ab], 0
    je vp_setpair
    mov bx, VP_PAGEA
    mov al, 4
; vp_setpair - BX = the start address, AL = the pel panning: written
; together, interrupts off, nothing between
vp_setpair:
    mov cl, al
    mov dx, 3D4h
    pushf
    cli
    mov al, 0Ch
    mov ah, bh
    out dx, ax
    inc al
    mov ah, bl
    out dx, ax
    mov dx, 3DAh                    ; the attribute flip-flop
    in al, dx
    mov dx, 3C0h
    mov al, 33h                     ; AR13 with the display left on
    out dx, al
    mov al, cl
    out dx, al
    popf
    ret

; vp_vsrise - wait for vertical sync to begin (out of it, then into it)
vp_vsrise:
    mov dx, 3DAh
    mov cx, 0FFFFh
.a:
    in al, dx
    test al, 8
    loopnz .a
    mov cx, 0FFFFh
.b:
    in al, dx
    test al, 8
    loopz .b
    ret

; vp_measure - the frame by the PIT: [vp_frame] vsync start to vsync start,
; [vp_vslen] its length, [vp_vsact] vsync start to the first displayed line,
; [vp_lines] the displayed scanlines
vp_measure:
    call vp_vsrise
    call pit_now
    mov [vp_t0], ax
    mov dx, 3DAh
.e:
    in al, dx
    test al, 8
    jnz .e
    call pit_now
    sub ax, [vp_t0]
    mov [vp_vslen], ax
    mov dx, 3DAh
.d:
    in al, dx
    test al, 1
    jnz .d
    call pit_now
    sub ax, [vp_t0]
    mov [vp_vsact], ax
    mov dx, 3DAh
    xor cx, cx
    pushf
    cli
.lc:
    in al, dx                       ; a displayed line...
    test al, 1
    jz .lc
    inc cx
.lb:
    in al, dx                       ; ...its blank, then the next, or the
    test al, 8                      ;   vsync
    jnz .ld
    test al, 1
    jnz .lb
    jmp short .lc
.ld:
    popf
    mov [vp_lines], cx
    call pit_now
    sub ax, [vp_t0]
    mov [vp_frame], ax
    ret

; vp_crtcsave - the stock vertical registers, for R to swap back
vp_crtcsave:
    mov si, vp_regs
    mov di, vp_stock
    mov cx, 5
    mov dx, 3D4h
.s:
    lodsb
    out dx, al
    inc dx
    push ax
    in al, dx
    stosb
    pop ax
    dec dx
    loop .s
    ret

; vp_timing - CR10, 11, 12, 15, 16: RealDMG's (R on) or the stock ones
vp_timing:
    call vp_unlock
    mov si, vp_stock
    cmp byte [vp_rb], 0
    je .w
    mov si, vp_rbv
.w:
    mov di, vp_regs
    mov cx, 5
    mov dx, 3D4h
.l:
    mov al, [di]
    mov ah, [si]
    cmp al, 11h                     ; (CR11's top half kept: the lock, the
    jne .o                          ;   vertical interrupt off)
    and ah, 0Fh
    mov bl, [vp_stock + 1]
    and bl, 0F0h
    or ah, bl
.o:
    out dx, ax
    inc si
    inc di
    loop .l
    ret

; vp_unlock - CR0-CR7 writable (CR11 bit 7)
vp_unlock:
    mov dx, 3D4h
    mov al, 11h
    out dx, al
    inc dx
    in al, dx
    and al, 7Fh
    out dx, al
    ret

; vp_split - [vp_spl] 0: the line compare 32 scanlines above the display's
; end, and AR10's pixel panning mode, so the status below neither scrolls
; nor pans; 1: the split without the panning mode; 2: no split at all (the
; status then rides in the pages, vp_status). The first field run found the
; Paradise reloading the panning AT THE SPLIT in mode 0
vp_split:
    call vp_unlock
    mov bx, 399 - 32
    cmp byte [vp_rb], 0
    je .s
    mov bx, 307 - 32
.s:
    cmp byte [vp_spl], 2
    jb .s2
    mov bx, 3FFh                    ; (never: no split)
.s2:
    mov dx, 3D4h
    mov al, 18h
    mov ah, bl
    out dx, ax
    mov al, 07h                     ; bit 8 in CR07 bit 4
    out dx, al
    inc dx
    in al, dx
    and al, 0EFh
    test bh, 1
    jz .b8
    or al, 10h
.b8:
    out dx, al
    dec dx
    mov al, 09h                     ; bit 9 in CR09 bit 6
    out dx, al
    inc dx
    in al, dx
    and al, 0BFh
    test bh, 2
    jz .b9
    or al, 40h
.b9:
    out dx, al
    mov dx, 3DAh                    ; AR10 bit 5
    in al, dx
    mov dx, 3C0h
    mov al, 30h
    out dx, al
    inc dx
    in al, dx
    and al, 0DFh
    cmp byte [vp_spl], 0
    jne .pm
    or al, 20h
.pm:
    dec dx
    out dx, al
    mov dx, 3D4h                    ; CR0-CR7 locked again
    mov al, 11h
    out dx, al
    inc dx
    in al, dx
    or al, 80h
    out dx, al
    ret

; vp_pages - the two pictures and a cleared status band. Page A: a line at
; x MOD 8 = 2, a rule every 16 rows, a diagonal at x = y + 40; page B the
; same 4 pixels left. Every plane alike: lit is colour 15
vp_pages:
    mov es, [vp_seg]
    mov dx, 3C4h
    mov ax, 0F02h
    out dx, ax
    xor di, di                      ; the status band and the gap, black
    mov cx, VP_PAGEA / 2
    xor ax, ax
    rep stosw
    mov bx, VP_PAGEA
    mov al, 20h
    mov si, 40
    call vp_pic
    mov bx, VP_PAGEB
    mov al, 02h
    mov si, 36
    call vp_pic
    ret
; vp_pic - BX = the page, AL = a line's byte, SI = the diagonal's x at y 0
vp_pic:
    xor dx, dx                      ; DX = y
.y:
    mov di, dx
    mov cx, di
    shl di, 1
    shl di, 1
    add di, cx                      ; y x 5
    shl di, 1
    shl di, 1
    shl di, 1                       ; y x 40
    add di, bx
    push di
    mov cx, 40
    mov ah, al
    test dl, 15
    jnz .f
    mov ah, 0FFh                    ; the rule
.f:
    push ax
    mov al, ah
    rep stosb
    pop ax
    pop di
    mov cx, si                      ; the diagonal's pixel
    add cx, dx
    push cx
    shr cx, 1
    shr cx, 1
    shr cx, 1
    add di, cx
    pop cx
    and cl, 7
    mov ah, 80h
    shr ah, cl
    or [es:di], ah
    inc dx
    cmp dx, 200
    jb .y
    ret

; vp_wrapdraw - stripes at F000-FFFF, checks at 0280-1FFF
vp_wrapdraw:
    mov es, [vp_seg]
    mov di, VP_WRAPS
    mov dx, 4096 / 40 + 1
.s:
    mov cx, 40
    mov al, 0FFh
    test dl, 2
    jz .a
    xor al, al
.a:
    rep stosb
    dec dx
    jnz .s
    mov di, 0280h
    mov dx, (2000h - 0280h) / 40
.c:
    mov ax, 0F0F0h                  ; checks, four rows a square
    test dl, 4
    jz .n
    not ax
.n:
    mov cx, 20
    rep stosw
    dec dx
    jnz .c
    ret

; vp_status - the two lines at VRAM 0, in the band below the split
vp_status:
    mov di, vp_s_st1
    mov si, vp_s_p
    call vp_strcpy
    mov ax, [vp_phase]
    call vp_us
    mov si, vp_s_f
    call vp_strcpy
    mov ax, [vp_frame]
    call vp_us
    mov si, vp_s_vs
    call vp_strcpy
    mov ax, [vp_vslen]
    call vp_us
    mov si, vp_s_va
    call vp_strcpy
    mov ax, [vp_vsact]
    call vp_us
    mov si, vp_s_l
    call vp_strcpy
    mov ax, [vp_lines]
    call vp_dec
    mov byte [di], 0
    mov si, vp_s_on
    cmp byte [vp_tog], 0
    jne .t
    mov si, vp_s_off
.t:
    cmp byte [vp_wrap], 0
    je .tw
    mov si, vp_s_wrap
.tw:
    mov di, vp_s_st2
    call vp_strcpy
    mov si, vp_s_rb0
    cmp byte [vp_rb], 0
    je .rb
    mov si, vp_s_rb1
.rb:
    call vp_strcpy
    mov bl, [vp_spl]
    xor bh, bh
    shl bx, 1
    mov si, [vp_s_spl + bx]
    call vp_strcpy
    mov byte [di], 0
    cmp byte [vp_spl], 2
    je .pg
    xor di, di
    mov si, vp_s_st1
    call vp_text
    mov di, 320
    mov si, vp_s_st2
    jmp vp_text
.pg:                                ; no split: in both pages' bottom rows,
    mov di, VP_PAGEB + 184 * 40     ;   page A's 4 pixels right, so the two
    mov si, vp_s_st1                ;   pairs show it in one place
    call vp_text
    mov di, VP_PAGEB + 192 * 40
    mov si, vp_s_st2
    call vp_text
    mov di, VP_PAGEA + 184 * 40
    mov si, vp_s_st1
    call vp_text4
    mov di, VP_PAGEA + 192 * 40
    mov si, vp_s_st2
    jmp vp_text4

; vp_text4 - vp_text 4 pixels to the right, ORed into eight rows it clears
vp_text4:
    mov es, [vp_seg]
    push di
    mov cx, 8 * 40 / 2
    xor ax, ax
    rep stosw
    pop di
    mov cx, 40
.c:
    lodsb
    or al, al
    jnz .g
    dec si
    mov al, ' '
.g:
    push cx
    push si
    push ds
    sub al, [vp_font0]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, [vp_fontp]
    add si, ax
    mov ds, [vp_fontp + 2]
    mov cx, 8
.r:
    lodsb
    mov ah, al
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    or [es:di], al
    shl ah, 1
    shl ah, 1
    shl ah, 1
    shl ah, 1
    or [es:di + 1], ah
    add di, 40
    loop .r
    pop ds
    sub di, 320 - 1
    pop si
    pop cx
    loop .c
    ret

; vp_text - SI = a line, DI = its VRAM offset: forty cells, padded
vp_text:
    mov es, [vp_seg]
    mov cx, 40
.c:
    lodsb
    or al, al
    jnz .g
    dec si
    mov al, ' '
.g:
    push cx
    push si
    push ds
    sub al, [vp_font0]
    xor ah, ah
    shl ax, 1
    shl ax, 1
    shl ax, 1
    mov si, [vp_fontp]
    add si, ax
    mov ds, [vp_fontp + 2]
    mov cx, 8
.r:
    movsb
    add di, 39
    loop .r
    pop ds
    sub di, 320 - 1
    pop si
    pop cx
    loop .c
    ret

; vp_report - the numbers into the window, for after
vp_report:
    mov si, vp_s_st1
    mov di, vp_s_res
    call vp_strcpy
    mov byte [di], 0
    ret

; vp_us - AX = PIT counts, as microseconds at DI, then a space
vp_us:
    mov cx, 838
    mul cx
    mov cx, 1000
    div cx
; vp_dec - AX in decimal at DI
vp_dec:
    push bx
    mov bx, 10
    xor cx, cx
.d:
    xor dx, dx
    div bx
    push dx
    inc cx
    or ax, ax
    jnz .d
.o:
    pop ax
    add al, '0'
    mov [di], al
    inc di
    loop .o
    pop bx
    ret

; vp_strcpy - SI to DI, without the NUL; DI left on the end
vp_strcpy:
    lodsb
    or al, al
    jz .r
    mov [di], al
    inc di
    jmp short vp_strcpy
.r:
    ret

%include "os88pit.inc"

vp_tpl:
    dw 100, 80, VP_CONT_W + 2, VP_CONT_H + TITLE_H + 2
    dw vp_ttl, vp_paint, vp_onkey, 0
vp_ttl:     db 'VGA Probe', 0
vp_legend:  dw vp_l1, vp_l2, vp_l3, vp_l4, vp_l5, vp_l6, vp_s_res, 0
vp_l1:      db 'Enter: run it (VGA, full screen). Inside:', 0
vp_l2:      db 'Left/Right phase 1 line, Up/Down 10, PgUp/Dn 100', 0
vp_l3:      db 'Space toggle, S split, W wrap, R RB timing, T time', 0
vp_l4:      db 'Still = both latched in one frame; shimmer =', 0
vp_l5:      db 'a frame apart; shimmer below a row = per line', 0
vp_l6:      db 'Last numbers (P F VS VA in us, L lines):', 0
vp_s_novga: db 'No VGA mode 0Dh here', 0
vp_s_p:     db 'P', 0
vp_s_f:     db ' F', 0
vp_s_vs:    db ' VS', 0
vp_s_va:    db ' VA', 0
vp_s_l:     db ' L', 0
vp_s_on:    db 'toggling', 0
vp_s_off:   db 'held (A)', 0
vp_s_wrap:  db 'WRAP: stripes,text,checks=wraps', 0
vp_s_rb0:   db ' stock', 0
vp_s_rb1:   db ' RealDMG', 0
vp_s_spl:   dw vp_s_sp0, vp_s_sp1, vp_s_sp2
vp_s_sp0:   db ' split+PPM', 0
vp_s_sp1:   db ' split', 0
vp_s_sp2:   db ' no split', 0
vp_spl:     db 0
vp_regs:    db 10h, 11h, 12h, 15h, 16h
vp_rbv:     db 6Eh, 00h, 33h, 68h, 8Bh   ; rwfsx.inc's rw_fscrv
vp_phase:   dw 0
vp_ab:      db 0
vp_s_res:   times 48 db 0

VP_BSS      equ 176                 ; (the bss below, summed)
    align 2, db 0
    OS88_BSS VP_BSS
    OS88_IMAGE_END
vp_caps     equ os88_image_end      ; word: OSAPI_FSX_CAPS's mask
vp_wx       equ vp_caps + 2         ; word: the window's content corner
vp_wy       equ vp_wx + 2
vp_seg      equ vp_wy + 2           ; word: the framebuffer
vp_fontp    equ vp_seg + 2          ; dword: the kernel's glyphs
vp_font0    equ vp_fontp + 4        ; byte: their first code
vp_tog      equ vp_font0 + 1        ; byte: toggling the pair
vp_wrap     equ vp_tog + 1          ; byte: the wrap test is up
vp_rb       equ vp_wrap + 1         ; byte: RealDMG's vertical timing
vp_t0       equ vp_rb + 1           ; word: vsync start, PIT counts
vp_frame    equ vp_t0 + 2           ; word: the four timings, PIT counts,
vp_vslen    equ vp_frame + 2        ;   and the displayed scanlines
vp_vsact    equ vp_vslen + 2
vp_lines    equ vp_vsact + 2
vp_stock    equ vp_lines + 2        ; 6: CR10, 11, 12, 15, 16 as the mode set
vp_info     equ vp_stock + 6        ; FSI_SIZE
vp_s_st1    equ vp_info + FSI_SIZE  ; 64: the status's two lines
vp_s_st2    equ vp_s_st1 + 64
%if vp_s_st2 + 64 - vp_caps != VP_BSS
%error "VP_BSS is not the bss's sum"
%endif
