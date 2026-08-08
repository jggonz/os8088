; =============================================================================
; splash.inc - boot-time loading screen (SPEC.md 15)
;
; While the boot sector is still pulling the rest of this kernel off the
; floppy, it far-calls spl_tick (via the pinned 0800:0008 entry) after every
; sector that lands. The first tick switches to mode 12h and draws the
; welcome dialog; every tick advances a pixel-granular progress bar, a
; percentage, and a spinning vector "8088" - stroke digits coin-flipped
; about their vertical axis through a 16-entry cosine table.
;
; This module runs before ANY other module is resident, so it is entirely
; self-contained: BIOS int 10h for mode set and text, its own planar
; primitives for everything else. It must never slow the load down - it
; contains no waits and no timing loops; a completed sector read is the only
; thing that advances it, and the drawing (a few ms at 4.77MHz) happens
; inside the disk's rotational latency toward the next sector.
;
; State lives in in-module data words, never .bss: .bss starts at the image
; end, where the final sector read lands, and nothing has cleared it yet - so
; a .bss write here is a write into memory the loader is still filling. This
; module must stay off .bss.
; The build assertion at the bottom guarantees the module is aboard before
; the first tick can arrive.
; =============================================================================

SPL_RESIDENT equ 8              ; sectors the boot sector waits for before
                                ; ticking us (must match boot/boot.asm).
                                ; It was 6, and the module ended at 3,072
                                ; bytes EXACTLY - no headroom at all, so the
                                ; next thing that had to be resident (the
                                ; equipment-flag fix in viddet.inc, 32 bytes)
                                ; failed the assertion below rather than
                                ; silently ticking a splash that was not
                                ; aboard.
                                ;
                                ; It is 8 for the second instance of exactly
                                ; that, and the shape is worth recognising:
                                ; the assertion fires on whatever crosses the
                                ; line, not on whatever is to blame. This time
                                ; it was a MERGE - SPEC.md 39.6's 15-byte CGA
                                ; pre-clear in viddet.inc arriving on top of
                                ; the other branch's growth - and the module
                                ; ended 4 bytes over 3,584. Shaving four bytes
                                ; off a module that has twice filled its rung
                                ; buys one more collision; a sector buys 512.
                                ;
                                ; One more sector is one more BIOS read before
                                ; the bar starts moving: ~65 ms on the field
                                ; machine (PERFORMANCE.md's measured sector
                                ; inside a coalesced run), where the figure
                                ; this comment used to quote - a fifth of a
                                ; second - was a sector at the pre-18.91 238 ms
SPL_GC      equ 0x3CE           ; VGA Graphics Controller index port

; The bar counts SECTORS, and the boot sector's read is not the last of them:
; kmain's tail mounts A:, reads SYSTEM.CFG and loads whatever it asks for,
; which on the 1.44MB system disk is thirteen more sectors before a pixel of
; desktop can be drawn - about three seconds at PERFORMANCE.md's measured
; 65 ms per sector in a run (PERFORMANCE.md Part 2;
; it was 238 before SPEC.md 18.91's AL fix). The bar used to reach 100% at the last sector of the
; kernel and then sit there through all of it.
;
; So SPL_POST notches are added to the DENOMINATOR and spent after the load
; (SPEC.md 15.3): thirteen for that mount, three for the phases of kmain that
; are not I/O (the mouse's ~165 ms reset is the big one), and spl_finish
; forces the last. It is tuned for the common boot - a system disk whose
; SYSTEM.CFG asks for no driver - and it is a CLAMP, not a scale: a machine
; that has enabled the sound or disk driver spends twenty-seven more sectors
; here and the bar waits at 100% for them, which is what every boot used to
; do for all of it.
SPL_POST    equ 16

; Spinner: "8088" as 19 stroke segments in a 104x40 box, screen-centred at
; x=320 with its top at SPL_TOP. The coin-flip transform only ever scales x
; about the box centre, so strokes stay horizontal/vertical after it.
SPL_TOP     equ 118
SPL_HALFW   equ 52

; Dialog and bar geometry. The dialog is VGA-only and stays at its 640x480
; coordinates; the BAR is the one thing that draws on all three adapters, so
; its origin is computed at run time by spl_chrome (SPEC.md 39.6).
SPL_BAR_PX  equ 288             ; bar interior width in pixels
SPL_BAR_Y   equ 264             ; bar interior top row on VGA - inside the
                                ; trough spl_chrome draws at rows 262..281
SPL_BAR_H   equ 16

; -----------------------------------------------------------------------------
; spl_tick - the 0800:0008 far entry (SPEC.md 15).
; in: AX = sectors loaded, DX = total sectors. Preserves every register and
; segment; flags clobbered. Runs on the boot stack, boot sector's segments.
; -----------------------------------------------------------------------------
spl_tick:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    push cs
    pop ds                      ; our data lives in the kernel segment

    mov [spl_done], ax
    add dx, SPL_POST            ; kmain's tail is on this bar too (SPEC 15.3)
    mov [spl_total], dx

    cmp byte [spl_on], 0
    jne .running
    mov byte [spl_on], 1
    mov byte [spl_live], 1      ; ...and from here the splash OWNS the screen
    call spl_chrome             ; and the video mode, until spl_finish
.running:
    call spl_paint

    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; -----------------------------------------------------------------------------
; spl_paint - one frame of the loading screen, from [spl_done]/[spl_total].
;             clobbers: ax, bx, cx, dx, si, di, bp, es
;
; Factored out of spl_tick so the boot sector's ticks and kmain's post-load
; notches (spl_step) cannot disagree about what a frame is.
; -----------------------------------------------------------------------------
spl_paint:
    call spl_bar                ; the ONE stage that runs on all three
                                ; adapters - see spl_chrome
    cmp byte [vid_mono], 0
    jne .out
    call spl_pct
    call spl_spin
.out:
    ret

; -----------------------------------------------------------------------------
; spl_step - one post-load notch on the bar (SPEC.md 15.3)
; in:  nothing;  DS = KERNEL_SEG (every caller is kernel code)
; out: nothing - EVERY register and the flags preserved, because the hot
;      caller is the middle of dsk_xfer's per-sector loop
;
; A no-op unless the splash is on screen, which is the whole of its safety:
; after spl_finish this is a compare and a return, and it stays on the disk
; path for the life of the machine at that price (~13 us against a sector's
; 65 ms).
; -----------------------------------------------------------------------------
spl_step:
    pushf
    cmp byte [spl_live], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, [spl_done]
    inc ax
    cmp ax, [spl_total]
    jb .set
    mov ax, [spl_total]         ; the last notch is spl_finish's, so a boot
    dec ax                      ; that overruns its allowance crowds up
.set:                           ; against 100% instead of claiming to be done
    mov [spl_done], ax
    call spl_paint
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; spl_finish - the bar to 100%, and hand the screen over (SPEC.md 15.3)
; in:  nothing;  DS = KERNEL_SEG
; out: nothing (every register and the flags preserved)
;
; Called by kmain immediately before the first wm_paint_all, which is the last
; moment at which a full bar is still the truth: the desktop paint covers
; every pixel of the screen (the menu bar's field, the desktop dither over the
; dock strip, then the dock), so the loading screen needs no erase of its own.
;
; Clearing [spl_live] is also what re-arms vid_setmode: from here on the mode
; is the kernel's to change again (SPEC.md 39.1).
; -----------------------------------------------------------------------------
spl_finish:
    pushf
    cmp byte [spl_live], 0
    je .out
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov ax, [spl_total]
    mov [spl_done], ax
    call spl_paint
    mov byte [spl_live], 0
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
.out:
    popf
    ret

; -----------------------------------------------------------------------------
; spl_chrome - first tick only: mode 12h, the double-framed welcome dialog,
;              the bar trough, the title. clobbers: ax, bx, cx, dx, si, es
; -----------------------------------------------------------------------------
spl_chrome:
    call vid_detect             ; the adapter probe happens HERE, before any
    call vid_apply              ; other kernel module is even resident: the
    call vid_setmode            ; splash owns the first mode set, and kmain's
                                ; vid_init later re-runs all three (SPEC 39.1)

; ...and the same chrome onto a mode SOMEBODY ELSE has just set: spl_reset's
; entry, for when drv_boot moves the machine onto the adapter SYSTEM.CFG asks
; for while the loading screen is still up (SPEC.md 39.11.3). Entering above
; would re-run vid_detect, which answers with the PROBE's adapter and would
; undo the switch three instructions after it happened. Costs nothing: it is
; a label, and the geometry below is read live either way.
spl_rechrome:
    mov ax, [vid_h]             ; the bar's origin, centred on whatever screen
    shr ax, 1                   ; we got. On VGA this would be row 232, but
    cmp byte [vid_mono], 0      ; the bar has to sit inside the dialog's
    je .barv                    ; trough at 264, so VGA keeps its constant.
    sub ax, 8
    jmp short .bary
.barv:
    mov ax, SPL_BAR_Y
.bary:
    call gfx_rowbase
    mov bx, [vid_w]             ; x = (width - bar) / 2, in whole bytes: 22 at
    sub bx, SPL_BAR_PX          ; 640 (which is what SPL_BAR_ADR always was),
    mov cl, 4                   ; 27 at Hercules' 720
    shr bx, cl
    add ax, bx
    mov [spl_bar_adr], ax

    mov ax, [vid_seg]
    mov es, ax

    cmp byte [vid_mono], 0      ; everything below is 640x480 chrome drawn
    jne .mono                   ; with BIOS text: the dialog does not fit in
                                ; 200 rows, and int 10h teletype into a
                                ; Hercules card already in graphics writes
                                ; character/attribute pairs into the bitmap.
                                ; The bar alone carries the mono splash.

    mov cx, 152                 ; dialog: two nested 1px frames, the mode 3
    mov si, 487                 ; splash's double-line box reborn in pixels
    mov bx, 208
    mov dx, 343
    call spl_rect
    mov cx, 156
    mov si, 483
    mov bx, 212
    mov dx, 339
    call spl_rect

    mov cx, 174                 ; bar trough, 1px inset from the fill
    mov si, 465
    mov bx, 262
    mov dx, 281
    call spl_rect

    mov ah, 0x02                ; title via BIOS teletype (mode 12h text is
    xor bh, bh                  ; legal this early - SPEC.md 15): row 15,
    mov dx, (15 << 8) | 31      ; col 31 centres 18 chars on the 80-cell grid
    int 0x10
    mov si, spl_s_welcome
    jmp spl_print               ; tail call
.mono:
    ret

; -----------------------------------------------------------------------------
; spl_bar - fill the bar to done*SPL_BAR_PX/total pixels. The fill only ever
;           grows, so drawing just the current extent is complete.
; clobbers: ax, bx, cx, dx, si, di, es
; -----------------------------------------------------------------------------
spl_bar:
    mov ax, [vid_seg]
    mov es, ax

    mov ax, [spl_done]
    mov cx, SPL_BAR_PX
    mul cx
    div word [spl_total]        ; AX = filled pixels, 0..SPL_BAR_PX
    mov si, ax
    mov cl, 3
    shr ax, cl                  ; AX = whole bytes of fill

    mov di, [spl_bar_adr]
    mov cx, ax
    mov bx, SPL_BAR_H
    mov al, 0xFF
    jcxz .partial
    call spl_fill
    add di, cx                  ; spl_fill preserves DI/CX; step past the fill
.partial:
    and si, 7                   ; leftover pixels into the next byte
    jz .done
    mov cx, si
    neg cl
    add cl, 8
    mov al, 0xFF
    shl al, cl                  ; leading SI bits set
    call spl_bitmask
    mov bx, SPL_BAR_H
.prow:
    call spl_wrbyte             ; merge white through the armed mask
    call gfx_nextrow
    dec bx
    jnz .prow
    mov al, 0xFF
    call spl_bitmask            ; leave the GC at its default
.done:
    ret

; -----------------------------------------------------------------------------
; spl_pct - right-aligned percentage under the bar, via BIOS teletype.
; clobbers: ax, bx, cx, dx, si
; -----------------------------------------------------------------------------
spl_pct:
    mov ax, [spl_done]
    mov cx, 100
    mul cx
    div word [spl_total]        ; AX = percent, 0..100

    mov word [spl_pctbuf], '  '     ; digits grow leftward over the blanks
    mov bx, spl_pctbuf + 2
    mov cx, 10
.digit:
    xor dx, dx
    div cx
    add dl, '0'
    mov [bx], dl
    dec bx
    test ax, ax
    jnz .digit

    mov ah, 0x02                ; row 19, col 38: "100%" sits dead centre
    xor bh, bh
    mov dx, (19 << 8) | 38
    int 0x10
    mov si, spl_pctbuf
    ; fall through

; -----------------------------------------------------------------------------
; spl_print - BIOS-teletype the NUL-terminated string at DS:SI in bright
;             white. clobbers: ax
; -----------------------------------------------------------------------------
spl_print:
    push bx
    push si
    mov bx, 0x000F              ; page 0; BL = foreground in graphics modes
.next:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .next
.done:
    pop si
    pop bx
    ret

; -----------------------------------------------------------------------------
; spl_spin - one animation step: blank the spinner box, then redraw every
;            stroke with x scaled by cos(angle), angle = done mod 16 of a
;            revolution. cos < 0 mirrors the endpoints, so the back face
;            renders itself. clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
spl_spin:
    mov ax, [vid_seg]           ; VGA only - spl_tick gates the call
    mov es, ax

    mov di, SPL_TOP * 80 + 33   ; bounding box: x 264..383, 41 rows
    mov cx, 15
    mov bx, 41
    xor al, al
    call spl_fill

    mov bx, [spl_done]
    and bx, 15
    shl bx, 1
    mov ax, [spl_cos_tab + bx]
    mov [spl_cos], ax

    mov si, spl_segs
    mov cx, SPL_NSEGS
.seg:
    push cx
    lodsb                       ; x1
    call spl_xform
    mov bx, ax
    lodsb                       ; y1
    xor ah, ah
    mov dx, ax
    lodsb                       ; x2
    call spl_xform
    mov bp, ax
    lodsb                       ; y2
    xor ah, ah

    cmp dx, ax
    jne .vert
    mov cx, bx                  ; horizontal: y1 == y2
    mov bx, dx
    add bx, SPL_TOP
    mov dx, bp
    cmp cx, dx
    jbe .ordered
    xchg cx, dx                 ; mirrored frames swap the endpoints
.ordered:
    call spl_hline
    jmp .next
.vert:
    mov cx, bx                  ; vertical: x1 == x2, y1 < y2 by data order
    mov bx, dx
    add bx, SPL_TOP
    mov dx, ax
    add dx, SPL_TOP
    call spl_vline
.next:
    pop cx
    loop .seg
    ret

; -----------------------------------------------------------------------------
; spl_xform - coin-flip a spinner x: AL = box-relative x (0..104) ->
;             AX = 320 + (AL - SPL_HALFW) * cos / 64. clobbers: nothing else
; -----------------------------------------------------------------------------
spl_xform:
    push cx
    push dx
    xor ah, ah
    sub ax, SPL_HALFW
    imul word [spl_cos]         ; DX:AX = rel * cos; |result| <= 3328
    mov cl, 6
    sar ax, cl
    add ax, 320
    pop dx
    pop cx
    ret

; -----------------------------------------------------------------------------
; spl_hline - white pixel-granular horizontal line, CX = x1, DX = x2
;             (x1 <= x2), BX = y. ES = VGA_SEG. preserves all registers
; -----------------------------------------------------------------------------
spl_hline:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    mov si, dx                  ; SI = x2 (gfx_rowbase eats CX and DX)
    mov ax, bx                  ; AX = y
    mov bx, cx                  ; BX = x1 - gfx_rowbase preserves it
    call gfx_rowbase
    mov bp, ax                  ; BP = row base

    mov ax, bx
    mov cl, 3
    shr ax, cl
    add bp, ax                  ; BP = first byte address
    mov dx, si
    shr dx, cl
    sub dx, ax                  ; DX = whole-byte span (0 = single byte)

    mov al, 0xFF                ; left edge mask: 0xFF >> (x1 & 7)
    mov cx, bx
    and cl, 7
    shr al, cl
    mov ah, 0xFF                ; right edge mask: 0xFF << (7 - (x2 & 7))
    mov cx, si
    and cl, 7
    xor cl, 7
    shl ah, cl

    mov di, bp
    or dx, dx
    jnz .multi
    and al, ah                  ; both edges share the byte
    call spl_maskbyte
    jmp .out
.multi:
    push ax
    call spl_maskbyte           ; left edge (advances DI)
    mov al, 0xFF
    call spl_bitmask
    mov cx, dx
    dec cx
    jcxz .right
    mov al, 0xFF                ; interior: plain full-byte writes
    rep stosb
.right:
    pop ax
    mov al, ah
    call spl_maskbyte
.out:
    mov al, 0xFF
    call spl_bitmask

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; spl_vline - white 1px vertical line, CX = x, BX = y1, DX = y2 (y1 <= y2).
;             ES = VGA_SEG. preserves all registers
; -----------------------------------------------------------------------------
spl_vline:
    push ax
    push bx
    push cx
    push dx
    push di

    mov di, dx
    sub di, bx                  ; DI = row count - 1
    inc di                      ; DI = row count
    push cx                     ; CX = x, and gfx_rowbase eats CX
    mov ax, bx
    call gfx_rowbase
    mov bx, di                  ; BX = row count
    mov di, ax                  ; DI = row base
    pop cx
    mov ax, cx
    push cx
    mov cl, 3
    shr ax, cl
    pop cx
    add di, ax                  ; DI = byte address

    and cl, 7
    mov al, 0x80
    shr al, cl                  ; single-pixel mask
    call spl_bitmask
.row:
    call spl_wrbyte             ; latch, then merge the masked bit
    call gfx_nextrow
    dec bx
    jnz .row
    mov al, 0xFF
    call spl_bitmask

    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; spl_rect - 1px white outline: CX = x1, SI = x2, BX = y1, DX = y2.
;            preserves all registers
; -----------------------------------------------------------------------------
spl_rect:
    push bx
    push dx
    mov dx, si
    call spl_hline              ; top
    pop bx                      ; BX = y2 (was DX on entry)
    push bx
    mov dx, si
    call spl_hline              ; bottom
    pop dx
    pop bx
    push cx
    call spl_vline              ; left
    mov cx, si
    call spl_vline              ; right
    pop cx
    ret

; -----------------------------------------------------------------------------
; spl_fill - byte-granular fill: ES:DI = top-left, CX = width in bytes,
;            BX = rows, AL = value. preserves all registers
; -----------------------------------------------------------------------------
spl_fill:
    push bx
    push cx
    push di
.row:
    push cx
    push di
    rep stosb
    pop di
    pop cx
    call gfx_nextrow
    dec bx
    jnz .row
    pop di
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; spl_bitmask - program the GC bit mask register from AL. preserves all
; -----------------------------------------------------------------------------
spl_bitmask:
    push ax
    push dx
    cmp byte [vid_mono], 0
    jne .mono
    mov ah, al
    mov al, 8                   ; GC index 8 = bit mask
    mov dx, SPL_GC
    out dx, ax
    pop dx
    pop ax
    ret
.mono:
    mov [spl_mask], al          ; no GC to arm: spl_wrbyte applies it in
    pop dx                      ; software
    pop ax
    ret

; -----------------------------------------------------------------------------
; spl_maskbyte - merge white pixels through the mask in AL at ES:DI, then
;                advance DI one byte. clobbers: al
; -----------------------------------------------------------------------------
spl_maskbyte:
    call spl_bitmask
    call spl_wrbyte
    inc di
    ret

; -----------------------------------------------------------------------------
; spl_wrbyte - merge white pixels through the armed mask at ES:DI, without
;              advancing. VGA: read to load the latches, then write FF and
;              let the Bit Mask decide which bits land. Mono: the mask is a
;              byte in RAM and the merge is an OR. clobbers: al
; -----------------------------------------------------------------------------
spl_wrbyte:
    cmp byte [vid_mono], 0
    jne .mono
    mov al, [es:di]             ; load latches
    mov byte [es:di], 0xFF
    ret
.mono:
    mov al, [spl_mask]
    or [es:di], al
    ret

; -----------------------------------------------------------------------------
; data (in .text on purpose - .bss is off-limits here, see the header)
; -----------------------------------------------------------------------------

; "8088" stroke digits: 20 wide, 40 tall, on 28px centres.
%macro SPL_DIG8 1
    db %1,      0,  %1,      40
    db %1 + 20, 0,  %1 + 20, 40
    db %1,      0,  %1 + 20, 0
    db %1,      20, %1 + 20, 20
    db %1,      40, %1 + 20, 40
%endmacro
%macro SPL_DIG0 1
    db %1,      0,  %1,      40
    db %1 + 20, 0,  %1 + 20, 40
    db %1,      0,  %1 + 20, 0
    db %1,      40, %1 + 20, 40
%endmacro

spl_segs:
    SPL_DIG8 0
    SPL_DIG0 28
    SPL_DIG8 56
    SPL_DIG8 84
SPL_NSEGS equ ($ - spl_segs) / 4

spl_cos_tab:                    ; cos(k/16 rev) * 64, k = 0..15
    dw 64, 59, 45, 24, 0, -24, -45, -59
    dw -64, -59, -45, -24, 0, 24, 45, 59

spl_bar_adr dw 0            ; the bar's top-left byte, set by spl_chrome
spl_mask    db 0xFF          ; mono: what the GC's Bit Mask holds on VGA
spl_on      db 0             ; the chrome has been drawn - spl_tick's latch
spl_live    db 0             ; ...and the splash still OWNS the screen and the
                             ; video mode. A different question, and it stays
                             ; true right through kmain: spl_finish clears it
                             ; (SPEC.md 15.3)
spl_done    dw 0
spl_total   dw 1
spl_cos     dw 64
spl_pctbuf  db '   %', 0
spl_s_welcome db 'Welcome to os8088.', 0

; The whole module must be aboard before the boot sector's first tick.
spl_module_end:
SPL_MODULE_END equ spl_module_end - $$
%if SPL_MODULE_END > SPL_RESIDENT * 512
%error "splash: module must fit inside the first SPL_RESIDENT sectors (SPEC.md 15)"
%endif
