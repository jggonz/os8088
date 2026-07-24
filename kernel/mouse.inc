; =============================================================================
; jop - mouse.inc
;
; Serial Microsoft mouse on COM1 (0x3F8, IRQ4 -> int 0x0C) plus the arrow
; cursor with save-under. The ISR decodes the 3-byte Microsoft packet, keeps
; mouse_x/mouse_y/mouse_btn fresh, queues EVT_MDOWN/EVT_MUP on left-button
; changes, and moves the cursor itself only when the gfx lock is free and the
; cursor is refcount-visible (SPEC 7); otherwise it just sets cur_dirty.
;
; cursor_show/cursor_hide are refcounted via cur_level (-1 = hidden at boot,
; classic Show/HideCursor style) and guard their draw/restore sections with
; pushf/cli ... popf so the ISR never sees a half-drawn cursor. Both are
; called with the gfx lock held (from gfx_lock/gfx_unlock) or at boot.
;
; The hotspot is the drawn position (0,0 of the cell); at the right and
; bottom screen edges the cell is clipped, never shifted;
; mouse_x/mouse_y themselves range over the full 0..639 / 0..479.
; =============================================================================

MOU_COM1    equ 0x3F8           ; COM1 base: +1 IER, +2 IIR, +3 LCR, +4 MCR,
                                ;            +5 LSR (DLAB: +0 DLL, +1 DLM)
MOU_INTVEC  equ 0x30            ; int 0x0C vector offset in the IVT

CUR_W       equ 16              ; cursor cell size, px
CUR_H       equ 16
CUR_SAVE_SZ equ 192             ; 3 bytes wide x 16 rows x 4 planes

; -----------------------------------------------------------------------------
; mouse_init - hook int 0x0C, program the UART (1200 baud 7N1, RX interrupt,
;              DTR|RTS|OUT2 so the mouse powers up), flush stale RX/LSR/IIR,
;              unmask IRQ4 at the 8259.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mouse_init:
    push ax
    push dx
    push es

    ; --- save the old int 0x0C vector, install ours --------------------------
    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [es:MOU_INTVEC]
    mov [mou_old_off], ax
    mov ax, [es:MOU_INTVEC+2]
    mov [mou_old_seg], ax
    mov word [es:MOU_INTVEC], mou_isr
    mov [es:MOU_INTVEC+2], cs
    popf

    ; --- UART: 1200 baud, 7 data bits, no parity, 1 stop ---------------------
    mov dx, MOU_COM1+3          ; LCR: DLAB=1 + 7N1 bits
    mov al, 0x82
    out dx, al
    mov dx, MOU_COM1            ; divisor low = 96 (115200/96 = 1200)
    mov al, 96
    out dx, al
    mov dx, MOU_COM1+1          ; divisor high = 0
    xor al, al
    out dx, al
    mov dx, MOU_COM1+3          ; LCR: DLAB=0, 7N1
    mov al, 0x02
    out dx, al
    mov dx, MOU_COM1+4          ; MCR: DTR|RTS|OUT2 - powers the mouse and
    mov al, 0x0B                ; gates the IRQ onto the bus
    out dx, al

    ; --- drain the identify burst (IER still 0, IRQ4 still masked) -----------
    ; The DTR/RTS edge makes a real Microsoft mouse announce itself: 'M',
    ; maybe '3', maybe a whole PnP string - bytes with bit 6 set that the
    ; ISR's resync rule would misread as packet headers (phantom clicks,
    ; cursor jumps). Poll-and-discard for ~1 second before enabling RX
    ; interrupts; sched_init already runs, so [ticks] is live.
    push bx
    mov bx, [ticks]
.settle:
    mov dx, MOU_COM1+5          ; LSR: RX data ready?
    in  al, dx
    test al, 0x01
    jz .nochar
    mov dx, MOU_COM1
    in  al, dx                  ; discard identify/PnP byte
.nochar:
    mov ax, [ticks]
    sub ax, bx
    cmp ax, 19                  ; ~1s at 18.2Hz covers 'M3'+PnP at 1200 7N1
    jb .settle
    pop bx

    mov dx, MOU_COM1+1          ; IER: RX data available only
    mov al, 0x01
    out dx, al

    ; --- flush whatever is stale (IRQ4 still masked, so no race) -------------
    mov dx, MOU_COM1            ; RX
    in  al, dx
    mov dx, MOU_COM1+5          ; LSR
    in  al, dx
    mov dx, MOU_COM1+2          ; IIR
    in  al, dx

    mov byte [mou_phase], 0

    ; --- unmask IRQ4 ----------------------------------------------------------
    in  al, 0x21
    and al, 0xEF
    out 0x21, al

    pop es
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mouse_unhook - silence the UART (IER=0), mask IRQ4 again, restore the old
;                int 0x0C vector. Used before reboot.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
mouse_unhook:
    push ax
    push dx
    push es

    mov dx, MOU_COM1+1          ; IER = 0: no more UART interrupts
    xor al, al
    out dx, al

    in  al, 0x21                ; mask IRQ4
    or  al, 0x10
    out 0x21, al

    xor ax, ax
    mov es, ax
    pushf
    cli
    mov ax, [mou_old_off]
    mov [es:MOU_INTVEC], ax
    mov ax, [mou_old_seg]
    mov [es:MOU_INTVEC+2], ax
    popf

    pop es
    pop dx
    pop ax
    ret

; -----------------------------------------------------------------------------
; mou_isr - IRQ4 handler. One RX byte per interrupt. Microsoft protocol:
;           byte 0 has bit 6 set: 1 LB RB Y7 Y6 X7 X6; bytes 1/2 have bit 6
;           clear: low 6 bits of X then Y. Any bit-6 byte restarts the packet
;           (resync). Runs with IF=0 throughout; never sti (SPEC 7).
; -----------------------------------------------------------------------------
mou_isr:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push ds
    push es
    mov ax, KERNEL_SEG
    mov ds, ax
    cld

    mov dx, MOU_COM1
    in  al, dx

    test al, 0x40               ; bit 6 set -> first byte of a packet
    jz .cont
    mov [mou_b0], al
    mov byte [mou_phase], 1
    jmp .eoi

.cont:
    cmp byte [mou_phase], 1
    jne .chk2
    mov [mou_b1], al            ; second byte: X low 6 bits
    mov byte [mou_phase], 2
    jmp .eoi

.chk2:
    cmp byte [mou_phase], 2     ; anything else is a stray byte - drop it
    jne .eoi
    mov byte [mou_phase], 0     ; third byte: Y low 6 bits, packet complete
    mov bl, al                  ; BL = Y low bits, decoded below

    ; --- dx = sign-extended {X7X6 from b0, X5..X0 from b1} -------------------
    mov al, [mou_b0]
    and al, 0x03
    mov cl, 6
    shl al, cl                  ; X7X6 into bits 7:6
    mov ah, [mou_b1]
    and ah, 0x3F
    or  al, ah
    cbw                         ; AX = dx, signed
    add ax, [mouse_x]
    or  ax, ax                  ; clamp 0..SCREEN_W-1
    jns .xlo
    xor ax, ax
.xlo:
    cmp ax, SCREEN_W-1
    jle .xok
    mov ax, SCREEN_W-1
.xok:
    mov [mouse_x], ax

    ; --- dy = sign-extended {Y7Y6 from b0, Y5..Y0 from b2}; positive = down --
    mov al, [mou_b0]
    and al, 0x0C
    mov cl, 4
    shl al, cl                  ; Y7Y6 into bits 7:6
    and bl, 0x3F
    or  al, bl
    cbw                         ; AX = dy, signed
    add ax, [mouse_y]
    or  ax, ax                  ; clamp 0..SCREEN_H-1
    jns .ylo
    xor ax, ax
.ylo:
    cmp ax, SCREEN_H-1
    jle .yok
    mov ax, SCREEN_H-1
.yok:
    mov [mouse_y], ax

    ; --- buttons: b0 bit 5 = left, bit 4 = right -----------------------------
    mov al, [mou_b0]
    xor bl, bl
    test al, 0x20
    jz .nol
    or  bl, 0x01
.nol:
    test al, 0x10
    jz .nor
    or  bl, 0x02
.nor:
    mov bh, [mouse_btn]
    mov [mouse_btn], bl
    xor bh, bl                  ; changed bits
    test bh, 0x01               ; left button changed?
    jz .move

    mov si, mou_evrec           ; queue EVT_MDOWN / EVT_MUP, a=x b=y (SPEC 10)
    mov word [si], EVT_MUP
    test bl, 0x01
    jz .fill
    mov word [si], EVT_MDOWN
.fill:
    mov ax, [mouse_x]
    mov [si+2], ax
    mov ax, [mouse_y]
    mov [si+4], ax
    mov word [si+6], 0
    call evq_push               ; guards internally with pushf/cli; IF stays 0

.move:
    ; --- cursor: draw only if the lock is free AND cur_level >= 0 (SPEC 7) ---
    cmp byte [gfx_lock_flag], 0
    jne .dirty
    mov al, [cur_level]
    or  al, al
    js .dirty

    mov ax, [mouse_x]           ; hotspot = drawn position; the cell is
    mov bx, [mouse_y]           ; clipped at the screen edges in cur_draw
    cmp ax, [cur_drawn_x]       ; button-only packet: nothing to redraw
    jne .redraw
    cmp bx, [cur_drawn_y]
    je .eoi
.redraw:
    push ax
    push bx
    call cur_restoreu           ; erase at the old position
    pop bx
    pop ax
    mov [cur_drawn_x], ax
    mov [cur_drawn_y], bx
    call cur_saveu              ; save under + draw at the new one
    call cur_draw
    mov byte [cur_dirty], 0
    jmp .eoi

.dirty:
    mov byte [cur_dirty], 1     ; cursor_show catches up later

.eoi:
    mov al, 0x20                ; EOI - the BIOS does not handle IRQ4 for us
    out 0x20, al
    pop es
    pop ds
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    iret

; -----------------------------------------------------------------------------
; cursor_show - increment cur_level; on the -1 -> 0 transition, save under and
;               draw the cursor at the latest mouse position (clipped) and
;               clear cur_dirty. Interrupt-safe: the critical section runs
;               under pushf/cli so the ISR never sees a half-drawn state.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
cursor_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    inc byte [cur_level]
    jnz .done                   ; only the -1 -> 0 edge draws

    mov ax, [mouse_x]           ; hotspot = drawn position; the cell is
    mov bx, [mouse_y]           ; clipped at the screen edges in cur_draw
    mov [cur_drawn_x], ax
    mov [cur_drawn_y], bx
    mov byte [cur_dirty], 0
    call cur_saveu
    call cur_draw
.done:
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cursor_hide - decrement cur_level; on the 0 -> -1 transition, restore the
;               save-under (the buffer is only valid while the cursor is
;               drawn). Same pushf/cli guard as cursor_show.
; ins:  none
; outs: none (all registers preserved)
; -----------------------------------------------------------------------------
cursor_hide:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    pushf
    cli
    dec byte [cur_level]
    cmp byte [cur_level], -1
    jne .done                   ; only the 0 -> -1 edge erases
    call cur_restoreu
.done:
    popf
    pop es
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; cur_saveu - copy the 16x16 cell under (cur_drawn_x, cur_drawn_y) into
;             cur_save via gfx_save. Internal; caller guarantees IF=0 or the
;             gfx lock. gfx_save clips the rect at the screen edges the
;             same way gfx_restore does, so partial cells stay coherent.
; clobbers: ax, bx, cx, dx, di
; -----------------------------------------------------------------------------
cur_saveu:
    push es
    mov ax, [cur_drawn_x]
    mov bx, [cur_drawn_y]
    mov cx, ax
    add cx, CUR_W-1
    mov dx, bx
    add dx, CUR_H-1
    push ds
    pop es                      ; buffer lives in kernel .bss
    mov di, cur_save
    call gfx_save
    pop es
    ret

; -----------------------------------------------------------------------------
; cur_restoreu - write cur_save back under (cur_drawn_x, cur_drawn_y).
; clobbers: ax, bx, cx, dx, si
; -----------------------------------------------------------------------------
cur_restoreu:
    push es
    mov ax, [cur_drawn_x]
    mov bx, [cur_drawn_y]
    mov cx, ax
    add cx, CUR_W-1
    mov dx, bx
    add dx, CUR_H-1
    push ds
    pop es
    mov si, cur_save
    call gfx_restore
    pop es
    ret

; -----------------------------------------------------------------------------
; cur_draw - paint the arrow at (cur_drawn_x, cur_drawn_y): white pass with
;            the cur_and outline mask, black pass with the cur_data body.
;            Set/Reset supplies the color; the Bit Mask register carries the
;            row bits, shifted across up to 3 VRAM bytes; a read of each
;            target byte loads the latches before the masked write. Leaves
;            the GC in the default state (SPEC 1 rule 7). Internal; caller
;            guarantees IF=0 and default GC state on entry.
; clobbers: ax, bx, cx, dx, si, di, bp, es
; -----------------------------------------------------------------------------
cur_draw:
    mov ax, [cur_drawn_y]       ; DI = y*80 + x/8
    mov cl, 4
    shl ax, cl                  ; y*16
    mov di, ax
    shl ax, 1
    shl ax, 1                   ; y*64
    add di, ax
    mov bx, [cur_drawn_x]
    mov ax, bx
    mov cl, 3
    shr ax, cl
    add di, ax
    mov cl, bl
    and cl, 7                   ; CL = shift (x & 7)
    mov ch, 8
    sub ch, cl                  ; CH = 8 - shift

    ; --- clip the cell at the right and bottom screen edges ------------------
    ; AX still holds the byte column (x/8). bytes-in-row = 80 - column; skip
    ; the DI+1 / DI+2 writes that would wrap onto the next scanline.
    mov dx, ROW_BYTES
    sub dx, ax                  ; DX = bytes available in this row (>= 1)
    mov byte [cur_b1ok], 0
    mov byte [cur_b2ok], 0
    cmp dx, 2
    jb .cols_done
    mov byte [cur_b1ok], 1
    cmp dx, 3
    jb .cols_done
    mov byte [cur_b2ok], 1
.cols_done:
    mov ax, SCREEN_H
    sub ax, [cur_drawn_y]       ; rows on screen at this y (>= 1)
    cmp ax, CUR_H
    jbe .rows_set
    mov ax, CUR_H
.rows_set:
    mov [cur_rows], al

    mov ax, VGA_SEG
    mov es, ax

    mov dx, 0x3CE               ; GC1: enable Set/Reset on all planes
    mov ax, 0x0F01
    out dx, ax
    mov ah, CWHITE              ; GC0: Set/Reset color = white
    mov al, 0
    out dx, ax
    mov al, 8                   ; leave the index at GC8 (Bit Mask)
    out dx, al
    mov si, cur_and
    call cur_pass               ; white outline + fill

    mov dx, 0x3CE
    mov ah, CBLACK              ; GC0: Set/Reset color = black
    mov al, 0
    out dx, ax
    mov al, 8
    out dx, al
    mov si, cur_data
    call cur_pass               ; black body on top

    mov dx, 0x3CE               ; back to defaults: Set/Reset off, mask FF
    mov ax, 0x0001
    out dx, ax
    mov ax, 0xFF08
    out dx, ax
    ret

; -----------------------------------------------------------------------------
; cur_pass - one 16-row masked pass. Each 16-bit row lands at bit offset CL
;            inside a 24-bit window: B0:B1 = row >> CL, B2 = row << (8-CL).
;            Zero mask bytes are skipped.
; ins:  SI -> 16-dw row table, DI = VRAM offset of the top-left byte,
;       CL = shift, CH = 8-shift, ES = VGA_SEG,
;       GC: Set/Reset enabled + color set, index register = 8
; outs: none; preserves CX, DI; clobbers ax, bx, dx, si, bp
; -----------------------------------------------------------------------------
cur_pass:
    push di
    xor ax, ax
    mov al, [cur_rows]          ; clipped row count from cur_draw
    mov bp, ax
    mov dx, 0x3CF               ; GC data port; index already 8 (Bit Mask)
.row:
    lodsw                       ; AX = row bits, MSB = leftmost pixel
    mov bx, ax
    shr bx, cl                  ; BH:BL = B0:B1
    xchg cl, ch
    shl ax, cl                  ; AL = B2
    xchg cl, ch
    mov ah, al                  ; stash B2

    mov al, bh                  ; --- byte 0 ---
    or  al, al
    jz .b1
    out dx, al
    mov al, [es:di]             ; load latches
    mov [es:di], al
.b1:
    mov al, bl                  ; --- byte 1 ---
    or  al, al
    jz .b2
    cmp byte [cur_b1ok], 0      ; clipped off the right screen edge?
    je .b2
    out dx, al
    mov al, [es:di+1]
    mov [es:di+1], al
.b2:
    mov al, ah                  ; --- byte 2 ---
    or  al, al
    jz .next
    cmp byte [cur_b2ok], 0      ; clipped off the right screen edge?
    je .next
    out dx, al
    mov al, [es:di+2]
    mov [es:di+2], al
.next:
    add di, ROW_BYTES
    dec bp
    jnz .row
    pop di
    ret

; -----------------------------------------------------------------------------
; cursor bitmap - classic Mac arrow, hot spot (0,0), black body 11 rows tall
; with a 1px white outline. cur_and is the white pass (outline + fill),
; cur_data the black body drawn on top. MSB = leftmost pixel.
; -----------------------------------------------------------------------------
cur_and:                        ; white: body dilated by 1px in all directions
    dw 0xE000                   ; ***.............
    dw 0xF000                   ; ****............
    dw 0xF800                   ; *****...........
    dw 0xFC00                   ; ******..........
    dw 0xFE00                   ; *******.........
    dw 0xFF00                   ; ********........
    dw 0xFF00                   ; ********........
    dw 0xFF00                   ; ********........
    dw 0xFE00                   ; *******.........
    dw 0xFE00                   ; *******.........
    dw 0xDE00                   ; **.****.........
    dw 0x1E00                   ; ...****.........
    dw 0x0000
    dw 0x0000
    dw 0x0000
    dw 0x0000

cur_data:                       ; black: the arrow body itself
    dw 0x8000                   ; #...............
    dw 0xC000                   ; ##..............
    dw 0xE000                   ; ###.............
    dw 0xF000                   ; ####............
    dw 0xF800                   ; #####...........
    dw 0xFC00                   ; ######..........
    dw 0xFE00                   ; #######.........
    dw 0xF800                   ; #####...........
    dw 0xD800                   ; ##.##...........
    dw 0x8C00                   ; #...##..........
    dw 0x0C00                   ; ....##..........
    dw 0x0000
    dw 0x0000
    dw 0x0000
    dw 0x0000
    dw 0x0000

; -----------------------------------------------------------------------------
; module state (small - stays inline)
; -----------------------------------------------------------------------------
mouse_x     dw SCREEN_W/2       ; public: cursor position, 0..639
mouse_y     dw SCREEN_H/2       ; public: 0..479
mouse_btn   db 0                ; public: bit 0 = left, bit 1 = right

cur_level   db -1               ; refcount; < 0 = hidden (boot state)
cur_dirty   db 0                ; set when the ISR could not move the cursor
cur_drawn_x dw 0                ; where the cursor pixels actually are
cur_drawn_y dw 0                ; (valid only while cur_level >= 0)

mou_old_off dw 0                ; saved int 0x0C vector
mou_old_seg dw 0
mou_phase   db 0                ; packet state: 0 idle, 1 got b0, 2 got b1
mou_b0      db 0                ; packet byte 0: 1 LB RB Y7 Y6 X7 X6
mou_b1      db 0                ; packet byte 1: X low 6 bits
mou_evrec   times 8 db 0        ; scratch event record for evq_push (ISR only)

section .bss
cur_save    resb CUR_SAVE_SZ    ; save-under: 3 bytes x 16 rows x 4 planes
cur_rows    resb 1              ; edge clipping, set per draw by cur_draw:
cur_b1ok    resb 1              ; rows on screen, and whether the DI+1/DI+2
cur_b2ok    resb 1              ; bytes of each row are still inside the row
section .text
