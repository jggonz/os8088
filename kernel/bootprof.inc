; =============================================================================
; bootprof.inc - where a BOOT goes, measured by the machine itself
;                (SPEC.md 15.5).  `make BOOTPROF=1` ONLY.
;
; tools/os88boot.py answers this on an emulator and cannot answer it on iron:
; it needs a debug socket, a cycle counter and a symbol map, and a 5150 has
; none of the three.  This is the same question asked from inside, so the
; machine that matters can answer it - the phase table is drawn on the screen
; when the desktop is up, and published in SPEC.md 57's registry as 'BP' for
; anything reading from outside.
;
; THE CLOCK IS THE PIT, not [ticks].  A system tick is 54.9 ms and half the
; phases here are under 3 ms, so a tick-counted profile would report zero for
; everything except the four that matter.  The 32-bit stamp is
; `[ticks]*65536 + phase`, phase = 65536 - the latched counter: 0.8381 us a
; unit, and sch_account's arithmetic exactly (SPEC.md 8.1), IRR coherence
; check included.  It is COPIED rather than factored out of sch_account on
; purpose - that routine is on the task-switch path, and a `call`/`ret` there
; is 11 us of a 693 us switch that every SHIPPED kernel would then pay for a
; diagnostic no shipped kernel carries.
;
; Two consequences of using the PIT that a reader has to know:
;
;   * `make QUANTUM=` REPROGRAMS the divisor (SPEC.md 53.2.1), which is the one
;     thing this arithmetic cannot survive - the same reason sch_account pauses
;     while sch_fast is armed.  The Makefile refuses the pair.
;   * the clock only exists after `sched_init`, which is where mark 0 is.
;     Everything before it - the boot sector's load, dsk_boot_from, the CPU
;     probe, dsk_dpt_init - is one row measured in BIOS ticks off 0040:006C
;     against the boot sector's own t=0 (SPEC.md 15.4).  That phase is seconds
;     long, so a 54.9 ms quantum is 0.5% of it, and the boot sector has no
;     bytes to spare for anything better.
; =============================================================================
%ifdef BOOT_PROFILE

section .text

BP_MARKS   equ 11               ; stamps taken; BP_MARKS-1 intervals between
BP_HZDIV   equ 1193             ; PIT units per ms (1193182/1000, rounded)

; ---- the table, and the 'BP' block that names it (SPEC.md 57.2) -------------
; In .text with real initialisers rather than .bss, for dsk_dpt's reason
; (SPEC.md 18.92): `-f bin` zeroes nothing, and a mark that was never reached
; has to read as 0 rather than as whatever was in memory.
bprof_tab:   times BP_MARKS dd 0
bprof_boot:  dw 0xFFFF          ; boot sector + pre-sched init, in BIOS ticks.
                                ; 0xFFFF = the boot sector never stamped
                                ; [boot_ticks], so there was nothing to subtract

bprof_dbg_blk:
    dw DBG_TAG_BPROF            ; 'BP' - the tag is the first word too, so a
    dw bprof_tab                ; reader can check where its pointer landed
    dw BP_MARKS
    dw bprof_boot

; -----------------------------------------------------------------------------
; bprof_mark - stamp slot AL with the PIT clock (and, at slot 0, the ticks
;              the boot sector and everything before sched_init took)
; in:       AL = slot, 0 .. BP_MARKS-1;  DS = KERNEL_SEG
; out:      nothing
; clobbers: nothing (flags preserved - it sits between two calls in kmain and
;           must not be able to change what either of them did)
; -----------------------------------------------------------------------------
bprof_mark:
    pushf
    cli                         ; sch_account's contract, which the sequence
                                ; below is a copy of: the latch, the [ticks]
                                ; read and the IRR test are ONE observation.
                                ; With IRQ0 live between latch and read, the
                                ; tick lands after the (large) phase was
                                ; latched, the IRR bit is already served, no
                                ; correction fires, and the stamp is a whole
                                ; 54.9 ms high - rare, silent, and bigger
                                ; than most of the rows it lands in. The
                                ; popf at .out hands the caller's IF back
    push ax
    push bx
    push cx
    push dx
    cmp al, BP_MARKS
    jae .out
    mov ah, al                  ; AH keeps the slot; BX becomes its byte offset
    mov bl, al
    xor bh, bh
    shl bx, 1
    shl bx, 1

    mov al, 0x00                ; latch channel 0's count
    out 0x43, al
    in al, 0x40
    mov cl, al
    in al, 0x40
    mov ch, al
    neg cx                      ; CX = phase = 65536 - count
    mov dx, [ticks]
    mov al, 0x0A                ; OCW3: the next read of 0x20 is the IRR
    out 0x20, al
    in al, 0x20
    test al, 1                  ; IRQ0 pending?
    jz .cohere
    or cx, cx                   ; a large phase means the reload landed in the
    js .cohere                  ; us between latch and read - keep ticks
    inc dx                      ; ...a small one means the tick is not in yet
.cohere:
    mov [bx+bprof_tab], cx
    mov [bx+bprof_tab+2], dx

    or ah, ah                   ; slot 0 also closes the pre-PIT era
    jnz .out
    mov ax, [boot_ticks]        ; still the boot sector's t=0 here: kmain does
    cmp ax, 0xFFFF              ; not turn it into an elapsed count until the
    je .out                     ; first frame is on the glass (SPEC.md 15.4)
    push ds
    xor cx, cx
    mov ds, cx
    mov cx, [0x046C]            ; the same BIOS tick the boot sector read
    pop ds
    sub cx, ax
    mov [bprof_boot], cx
.out:
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    ret

; ---- the rows, in the order they are drawn ----------------------------------
; Twenty-one columns of label, so the value column starts at the same x on all
; three adapters and the whole box - 29 cells, 232px - still fits the
; narrowest of them twice over (SPEC.md 39: 640, 640 and 720 across).
bprof_l0:  db "boot + early init",0     ; the ticks row: everything before mark 0
bprof_lbl:                              ; ...then one per interval, mark N-1 -> N
    dw .a, .b, .c, .d, .e, .f, .g, .h, .i, .j
.a: db "clock + video + heap",0
.b: db "module table",0
.c: db "typeface",0
.d: db "windows + menus",0
.e: db "mouse_init",0
.f: db "desktop + drivers",0
.g: db "drv_boot",0
.h: db "xmem + theme",0
.i: db "first paint",0
.j: db "cursor + notice",0
bprof_ltot: db "TOTAL, OS only",0
bprof_lhdr: db "os8088 boot profile",0

section .cold

BP_X    equ 12                  ; the box, in pixels. Top left and small: it is
BP_Y    equ 26                  ; drawn over a painted desktop and the point is
BP_ROW  equ 10                  ; to photograph it, not to live with it
BP_VALX equ BP_X + 21*8

; -----------------------------------------------------------------------------
; bpf_bprof_show - draw the phase table over the finished desktop
; in:       nothing (far-called from kmain, DS = KERNEL_SEG, CS = COLD_SEG)
; out:      nothing
;
; LAST in kmain, after cursor_show and drv_notice, so nothing paints over it -
; and over the desktop rather than into a window, because a window is wm state
; that the user then has to close and this is a diagnostic that should vanish
; on the first repaint.
;
; Black ground, white text and no greys: on both 1bpp adapters a grey rounds
; to a checkerboard (SPEC.md 39.4), and a checkerboard behind 8x8 text is
; unreadable in a photograph, which is the whole delivery mechanism here.
; -----------------------------------------------------------------------------
bpf_bprof_show:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp

    call KERNEL_SEG:cw_gfx_lock
    mov byte [gfx_color], CBLACK
    mov ax, BP_X - 4
    mov bx, BP_Y - 4
    mov cx, BP_VALX + 8*8
    mov dx, BP_Y + (BP_MARKS + 3) * BP_ROW
    cmp cx, [vid_w]             ; the live geometry decides, never SCREEN_W
    jb .wok
    mov cx, [vid_w]
    dec cx
.wok:
    cmp dx, [vid_h]
    jb .hok
    mov dx, [vid_h]
    dec dx
.hok:
    call KERNEL_SEG:cw_gfx_fill
    mov byte [gfx_color], CWHITE

    mov si, bprof_lhdr          ; the header
    mov cx, BP_X
    mov dx, BP_Y
    call KERNEL_SEG:font_str

    mov bp, BP_Y + 2 * BP_ROW   ; row 1: the ticks era, before the PIT clock
    mov si, bprof_l0
    call bpf_label
    mov ax, [bprof_boot]
    cmp ax, 0xFFFF
    je .noboot
    call bpf_tickms
    jmp short .bootval
.noboot:
    xor ax, ax
.bootval:
    call bpf_value

    xor si, si                  ; ...then one row per interval
.next:
    add bp, BP_ROW
    mov bx, si
    shl bx, 1
    mov bx, [bx + bprof_lbl]
    push si
    mov si, bx
    call bpf_label
    pop si
    mov bx, si
    shl bx, 1
    shl bx, 1
    mov ax, [bx + bprof_tab + 4] ; the later stamp...
    mov dx, [bx + bprof_tab + 6]
    sub ax, [bx + bprof_tab]     ; ...minus the earlier one
    sbb dx, [bx + bprof_tab + 2]
    call bpf_ms
    call bpf_value
    inc si
    cmp si, BP_MARKS - 1
    jb .next

    add bp, BP_ROW              ; ...and what the kernel's own timer made of it
    mov si, bprof_ltot
    call bpf_label
    mov ax, [boot_ticks]        ; elapsed by now: kmain converted it above
    cmp ax, 0xFFFF
    je .nototal
    call bpf_tickms
    jmp short .totval
.nototal:
    xor ax, ax
.totval:
    call bpf_value
    call KERNEL_SEG:cw_gfx_unlock

    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    retf

; ---- bpf_label - draw ASCIIZ at DS:SI on row BP ------------------------------
bpf_label:
    push cx
    push dx
    mov cx, BP_X
    mov dx, bp
    call KERNEL_SEG:font_str
    pop dx
    pop cx
    ret

; ---- bpf_value - draw AX as "<n> ms", right of the label ---------------------
bpf_value:
    push cx
    push dx
    push si
    push di
    mov di, bprof_buf
    call fm_utoa                ; DI -> the NUL it stored
    mov word [di], ' m'         ; " ms",0 in three stores, since the digits
    mov word [di+2], 's'        ; end wherever they end
    mov si, bprof_buf
    mov cx, BP_VALX
    mov dx, bp
    call KERNEL_SEG:font_str
    pop di
    pop si
    pop dx
    pop cx
    ret

; ---- bpf_ms - DX:AX PIT units -> AX milliseconds ----------------------------
; A 32-bit quotient by the two-step idiom fm_ultoa uses, then the high half
; is asserted zero rather than assumed: 65535 ms is 65 seconds and a boot that
; long is a hang, so a value that does not fit is capped at 65535 and reads as
; obviously wrong instead of as a small plausible number.
bpf_ms:
    push bx
    push cx
    push dx
    push si
    mov cx, ax                  ; CX = the low half
    mov bx, BP_HZDIV
    mov ax, dx
    xor dx, dx
    div bx                      ; AX = high quotient, DX = its remainder
    mov si, ax                  ; ...kept to be asserted zero below
    mov ax, cx
    div bx                      ; (rem:lo)/1193 -> AX = the low quotient
    or si, si
    jz .out
    mov ax, 0xFFFF
.out:
    pop si
    pop dx
    pop cx
    pop bx
    ret

; ---- bpf_tickms - AX system ticks -> AX milliseconds ------------------------
; One tick IS 65536 PIT units, so the conversion is the register move below
; and bpf_ms does the rest - which is why the two clocks in this table need
; one divide between them and no second constant.
bpf_tickms:
    mov dx, ax
    xor ax, ax
    jmp bpf_ms

section .bss
bprof_buf:  resb 12             ; fm_utoa's 6 + " ms" + NUL, rounded up

section .text
%endif
