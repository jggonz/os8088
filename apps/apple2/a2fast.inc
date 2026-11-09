; =============================================================================
; os8088 - apps/apple2/a2fast.inc     the fast core's package half
;                                     (APPLE2-SPEC section 4.6)
;
; Part of APPLE2, an Apple II Plus emulator (docs/APPLE2-SPEC.md).
;
; THE FAST CORE IS PART 1 OF APPLE2.O88 - a 64KB segment that
; apps/m6502/m65gen.py writes, with the handler for opcode X at X x 256. This
; file is what the package keeps beside it:
;
;   _a2_run       the C's a2_run(cycles). With no fast core (a2_fseg = 0) it
;                 IS the accurate core (a2cpu.inc's a2_arun). With one it runs
;                 the fast core and, for what the fast core hands back, the
;                 accurate core - on the same _a2_m record and the same scratch
;                 page, so the two are one machine;
;   a2f_iord      the far doors the fast core calls the soft switches through,
;   a2f_iowr      which reach a2io.c's a2_io_rd / a2_io_wr as the accurate
;                 core's own call-outs do;
;   _a2f_image    the 64KB guest image the fast core runs in: RAM, the I/O
;                 page's trap bytes, slot space and the ROM.
;
; THE CONTRACT WITH THE GENERATED CORE (m65gen.py's header):
;   - entered far at A2F_ENTRY with AX = cycles, DX = the image's segment,
;     ES = this package's DS, BX = &_a2_m; answers AX = a reason and leaves
;     the machine in _a2_m (cnt included);
;   - the reasons: 0 the budget was spent, 2 hand back ONE instruction (the
;     one at a2_m.pc), 3 hand back the REGION - PC is in $C000-$CFFF (or at
;     $FFFE and up) and the accurate core runs until it leaves.
; =============================================================================

A2F_ENTRY    equ 0x0210             ; m65gen.py's ENTRY
A2F_FIXUP    equ 0x0203             ; ...its FIXUP: dw h_FE body, x_hb1
A2F_PARTLEN  equ 0xFE00             ; ...and PART_LEN
A2F_RSN_HB1  equ 2                  ; one instruction
A2F_RSN_HBR  equ 3                  ; the region: until PC leaves $C000-$CFFF
A2F_RSN_HBI  equ 4                  ; the interrupt flag's: a short run
A2F_HBI_RUN  equ 64                 ; ...this many cycles at most, so CLI's
                                    ; and PLP's one-instruction delays hold
A2_SCR_PV    equ 0x38               ; the fast core's V, B, D, I
A2_SCR_HBK   equ 0x3A               ; "stop when PC leaves $C000-$CFFF"
A2_SCR_CUTF  equ 0x3B               ; a2_cut() ran during this a2_run
A2_SCR_FEND  equ 0x50               ; the fast core's scratch ends here

section .bss
a2f_far:    resw 2                  ; the far pointer the call goes through
a2f_left:   resw 1                  ; cycles left of the run in progress
a2f_clkb:   resw 1                  ; CLKB across an accurate-core run
a2f_bud:    resw 1                  ; ...and that run's budget

section .text

; -----------------------------------------------------------------------------
; int a2_run(unsigned cycles) - APPLE2-SPEC section 4.2's contract, unchanged:
; A2_RUN_SLICE or A2_RUN_JAM, a2_m.cnt what was NOT spent.
; -----------------------------------------------------------------------------
_a2_run:
    cmp word [_a2_fseg], 0
    jne .fast
    jmp a2_arun                     ; the accurate core, the caller's frame

.fast:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov [a2_pkgds], ds
    mov ax, [bp+4]
    mov [a2f_left], ax
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:A2_SCR_BASE + A2_SCR_CUTF], 0
    mov byte [es:A2_SCR_BASE + A2_SCR_HBK], 0

.loop:
    mov ax, [_a2_fseg]
    mov word [a2f_far], A2F_ENTRY
    mov [a2f_far+2], ax
    ; THE FRAME (m65gen.py's header): this core's own segment, the
    ; translated ROM's PC table and code (F+28, F+26, F+24; 0 = none), the
    ; two doors, then the far call
    push word [_a2_fseg]
    push word [_a2_ftab]
    push word [_a2_faot]
    push cs
    mov ax, a2f_iowr
    push ax
    push cs
    mov ax, a2f_iord
    push ax
    mov ax, [a2f_left]
    mov dx, [_a2_m+AM_RAMSEG]
    mov bx, _a2_m
    push ds
    pop es
    call far [a2f_far]              ; (its retf takes the frame off)
    mov cx, [_a2_m+AM_CNT]
    mov [a2f_left], cx
    cmp ax, A2F_RSN_HB1
    jb .out                         ; the budget, spent (or cut)
    call .stop                      ; a cut, or nothing left?
    jc .slice
    cmp ax, A2F_RSN_HBR
    je .region

    ; --- A SHORT RUN ON THE ACCURATE CORE: one instruction (reason 2), or
    ; the interrupt flag's few (reason 4). CLKB moves for it, so that a2_now()
    ; inside it reads the clock this run would have had (CLKB - DEAD, DEAD
    ; starting at the short budget in place of `left`).
    mov cx, 1
    cmp ax, A2F_RSN_HBI
    jne .short
    mov cx, A2F_HBI_RUN
    cmp cx, [a2f_left]
    jle .short
    mov cx, [a2f_left]
.short:
    mov [a2f_bud], cx
    mov es, [_a2_m+AM_RAMSEG]
    mov ax, [es:A2_SCR_BASE + A2_SCR_CLKB]
    mov [a2f_clkb], ax
    mov ax, [a2f_left]
    sub ax, cx                      ; left - budget
    sub [es:A2_SCR_BASE + A2_SCR_CLKB], ax
    push cx
    call a2_arun
    add sp, 2
    mov es, [_a2_m+AM_RAMSEG]
    mov cx, [a2f_clkb]
    mov [es:A2_SCR_BASE + A2_SCR_CLKB], cx
    mov cx, [_a2_m+AM_CNT]          ; left -= budget - cnt
    add cx, [a2f_left]
    sub cx, [a2f_bud]
    mov [a2f_left], cx
    jmp short .back

.region:
    ; --- THE ACCURATE CORE UNTIL PC LEAVES $C000-$CFFF: a2cpu.inc's
    ; a2_rebias_go ends the run at the first re-bias out of the region
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:A2_SCR_BASE + A2_SCR_HBK], 1
    push word [a2f_left]
    call a2_arun
    add sp, 2
    mov es, [_a2_m+AM_RAMSEG]
    mov byte [es:A2_SCR_BASE + A2_SCR_HBK], 0
    mov cx, [_a2_m+AM_CNT]
    mov [a2f_left], cx

.back:
    cmp ax, A2_RUN_JAM
    je .out
    call .stop
    jnc .loop
.slice:
    xor ax, ax                      ; A2_RUN_SLICE
.out:
    mov cx, [a2f_left]
    mov [_a2_m+AM_CNT], cx
    mov es, [_a2_m+AM_RAMSEG]       ; ...and the countdown with it, so that
    mov [es:A2_SCR_BASE + A2_SCR_DEAD], cx  ; a2_now() after the run reads
                                    ; the clock the whole run reached
    mov [_a2_m+AM_REASON], ax
    pop es
    pop di
    pop si
    pop bp
    ret

; CF = the run is over: a2_cut() ran inside it, or nothing is left
.stop:
    push es
    mov es, [_a2_m+AM_RAMSEG]
    cmp byte [es:A2_SCR_BASE + A2_SCR_CUTF], 0
    pop es
    jne .yes
    cmp word [a2f_left], 0
    jle .yes
    clc
    ret
.yes:
    stc
    ret

; -----------------------------------------------------------------------------
; unsigned a2f_codesel(unsigned seg) - the selector to FAR CALL code in a
; claim through: the segment itself on a real-mode kernel, its pair's code
; half under kern_mega (os88api.inc's OSAPI_MEM_CODESEL). 0 = none.
; -----------------------------------------------------------------------------
; unsigned a2f_claimcode(void) - the fast core's 64KB code claim, 0 = refused.
; CONVENTIONAL on every kernel (MEMP_CONV, SPEC.md 50.3.7): on kern_mega that
; makes it an allocator pair whose code half OSAPI_MEM_CODESEL answers. The KB
; door sends an OS88_HIGHMEM package's 64KB above 1MB as a lone DATA selector,
; which CODESEL refuses - the fast core then never ran on kern_mega.
_a2f_claimcode:
    mov ax, 0x1000                  ; 64KB in paragraphs
    mov cx, MEMP_CONV
    mov bl, MEMP_CLAIM
    call OSAPI_MEM_P
    sbb ax, ax                      ; CF -> -1 / 0...
    not ax
    and ax, dx                      ; ...-> 0 = refused, or the claim
    ret

_a2f_codesel:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    call OSAPI_MEM_CODESEL
    jnc .ok
    xor ax, ax
.ok:
    pop bp
    ret

; -----------------------------------------------------------------------------
; a2f_iord - FAR, from the fast core: BX = an address in $C000-$C0FF -> DH.
; a2f_iowr - FAR: BX = the address, DH = the value.
; Every other register comes back - DS, the guest image, included.
; -----------------------------------------------------------------------------
a2f_iord:
    push ds
    push es
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    mov ds, [cs:a2_pkgds]
    push bx                         ; the cdecl argument
    call _a2_io_rd
    add sp, 2
    mov dh, al
    pop bp
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    pop es
    pop ds
    retf

a2f_iowr:
    push ds
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ds, [cs:a2_pkgds]
    mov al, dh
    xor ah, ah
    push ax                         ; the value
    push bx                         ; the address
    call _a2_io_wr
    add sp, 4
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop es
    pop ds
    retf

; -----------------------------------------------------------------------------
; void a2f_core(unsigned dstseg, unsigned partseg) - the fast core's 64KB code
; segment: the part's $FE00 bytes, then the $FE and $FF slots written as near
; jumps to the targets the part's FIXUP table names (m65gen.py, PART_LEN).
; -----------------------------------------------------------------------------
_a2f_core:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov es, [bp+4]
    mov ds, [bp+6]
    xor si, si
    xor di, di
    mov cx, A2F_PARTLEN / 2
    rep movsw                       ; cc8086:allow - DS, ES and DF set above
    mov ax, [A2F_FIXUP]             ; h_FE's body
    sub ax, 0xFE03
    mov byte [es:0xFE00], 0xE9      ; jmp near
    mov [es:0xFE01], ax
    mov ax, [A2F_FIXUP + 2]         ; x_hb1
    sub ax, 0xFF03
    mov byte [es:0xFF00], 0xE9
    mov [es:0xFF01], ax
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void a2f_image(unsigned newseg, unsigned oldseg, unsigned romseg)
;
; The fast core's 64KB guest image in NEWSEG: the 48K RAM and the scratch
; page from OLDSEG ($0000-$C1FF), the I/O page FILLED WITH A TRAP OPCODE
; ($02, which the fast core hands back - so a fetch there by fall-through or
; a branch from $BFxx reaches the accurate core, which fetches through the
; soft switches as the machine does), slot space $FF, and the main ROM at
; $D000-$FFFF out of the ROM part. The fast core's own scratch is zeroed.
; -----------------------------------------------------------------------------
_a2f_image:
    push bp
    mov bp, sp
    push si
    push di
    push ds
    push es
    cld
    mov es, [bp+4]
    mov ds, [bp+6]
    xor si, si
    xor di, di
    mov cx, (A2_SCR_BASE + 0x100) / 2
    rep movsw                       ; cc8086:allow - DS, ES and DF set above
    mov di, A2_IO_BASE
    mov cx, 0x80
    mov ax, 0x0202
    rep stosw                       ; cc8086:allow - the I/O page's trap
    mov di, A2_SCR_BASE + A2_SCR_END
    mov cx, (A2_SCR_FEND - A2_SCR_END) / 2
    xor ax, ax
    rep stosw                       ; cc8086:allow - the fast core's scratch
    mov di, A2_SCR_BASE + 0x100
    mov cx, (A2_ROM_BASE - A2_SCR_BASE - 0x100) / 2
    mov ax, 0xFFFF
    rep stosw                       ; cc8086:allow - slot space reads $FF
    mov ds, [bp+8]
    xor si, si
    mov di, A2_ROM_BASE
    mov cx, (0x10000 - A2_ROM_BASE) / 2
    rep movsw                       ; cc8086:allow - the main ROM
    ; THE KEYIN LOOP'S SHORTCUT (m65gen.py's kbd_idle) is armed only when the
    ; ROM holds that loop, byte for byte, at $FD1B
    push es
    pop ds
    xor al, al
    cmp word [0xFD1B], 0x4EE6       ; INC $4E
    jne .noidle
    cmp word [0xFD1D], 0x02D0       ; BNE +2
    jne .noidle
    cmp word [0xFD1F], 0x4FE6       ; INC $4F
    jne .noidle
    cmp word [0xFD21], 0x002C       ; BIT $C0..
    jne .noidle
    cmp word [0xFD23], 0x10C0       ; ..00 / BPL
    jne .noidle
    cmp byte [0xFD25], 0xF5         ; ...$FD1B
    jne .noidle
    inc al
.noidle:
    mov [A2_SCR_BASE + 0x4C], al
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret
