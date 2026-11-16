; =============================================================================
; os8088 - apps/c64/c64fast.inc       the fast core's package half
;                                     (C64-SPEC section 4.7)
;
; Part of C64, a reimplementation of VICE 3.10's `x64` (docs/C64-SPEC.md).
;
; THE FAST CORE IS PART 1 OF C64.O88 - apps/m6502/m65gen.py --machine c64,
; the handler for opcode X at X x 256 - and this is what the package keeps
; beside it: APPLE2's a2fast.inc one machine along.
;
;   _c64_run      the C's c64_run(cycles). With no fast core (c64_fseg = 0) it
;                 IS the accurate core (c64cpu.inc's c64_arun). With one it
;                 runs the fast core and, for what the fast core hands back,
;                 the accurate core - on the same _c64_m record and the same
;                 scratch, so the two are one machine;
;   c64f_iord     the far doors the fast core calls $D000-$DFFF through;
;   c64f_iowr
;   _c64f_core    the 64KB code segment, out of the part;
;   _c64f_codesel the selector a far call reaches it by.
;
; WHAT THE C64 ADDS TO THE APPLE II's PROTOCOL:
;   - NINE MORE FRAME WORDS: the fetch segment of each region of the bank
;     map ($0000-$9FFF and $C000-$CFFF; $A000-$BFFF; $D000-$DFFF;
;     $E000-$FFFF), 0 where PC there is handed back, and the CHARGEN's for a
;     data read - made here from MAPA/MAPD/MAPE and c64_rbset's segments,
;     per call, because only a hand-back can change the map - and four for
;     BASIC and the KERNAL translated ahead of time (C64-SPEC 4.8): each
;     bank's code selector, the PC table, and this core's own for the way
;     back;
;   - THE PROCESSOR PORT IN RAM WHILE THE FAST CORE RUNS: $0000/$0001 hold
;     the port's read-back (c64_io_rd's own answer) so a zero-page read is a
;     `mov`; the RAM bytes under it are kept here and put back after, so the
;     accurate core and the C never see the difference;
;   - CUTF and HBK are bits 0 and 1 of $FFF9, the fast core's flag byte (its
;     V, B, D and I are bits 2-6), the one spare byte of the accurate core's
;     scratch, where a guest write is already dropped.
; =============================================================================

C64F_ENTRY   equ 0x0210             ; m65gen.py's ENTRY
C64F_FIXUP   equ 0x0203             ; ...its FIXUP: dw h_FE body, x_hb1
C64F_PARTLEN equ 0xFE00             ; ...and PART_LEN
C64F_RSN_HB1 equ 2                  ; one instruction
C64F_RSN_HBR equ 3                  ; the region: until PC leaves it
C64F_RSN_HBI equ 4                  ; the interrupt flag's: a short run
C64F_HBI_RUN equ 64
C64F_PV      equ 0xFFF9             ; the flag byte
C64F_CUTF    equ 0x01               ; ...bit 0: c64_cut() ran during this run
C64F_HBK     equ 0x02               ; ...bit 1: stop when PC leaves the region

section .bss
c64f_far:   resw 2
c64f_left:  resw 1
c64f_bud:   resw 1
c64f_ram01: resw 1                  ; the RAM bytes under $00/$01

section .text

; -----------------------------------------------------------------------------
; int c64_run(unsigned cycles) - C64-SPEC section 4.5's contract, unchanged
; -----------------------------------------------------------------------------
_c64_run:
    cmp word [_c64_fseg], 0
    jne .fast
    jmp c64_arun                    ; the accurate core, the caller's frame

.fast:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov [c64_pkgds], ds
    call c64_rbset                  ; the ROM banks' fetch segments
    mov ax, [bp+4]
    mov [c64f_left], ax
    mov es, [_c64_m+CM_RAMSEG]
    and byte [es:C64F_PV], ~(C64F_CUTF | C64F_HBK)

.loop:
    call .portin                    ; $00/$01 read back, the RAM put aside
    mov ax, [_c64_fseg]
    mov word [c64f_far], C64F_ENTRY
    mov [c64f_far+2], ax
    ; THE FRAME (m65gen.py's header): the region words, the doors, the call
    mov es, [_c64_m+CM_RAMSEG]
    push word [_c64_fjit]           ; F+42 the recompiler (4.9, 0: none)
    push word [_c64_fakb]           ; F+40 the KERNAL's translation (4.8)
    push word [_c64_fseg]           ; F+38 the core, for the translation's way back
    push word [_c64_ftab]           ; F+36 the PC table
    push word [_c64_faot]           ; F+34 BASIC's translation (0: none)
    push word [c64_rbc]             ; F+32 the CHARGEN, for a data read
    mov ax, [_c64_m+CM_RAMSEG]
    cmp byte [es:C64_SCR_BASE + C64_SCR_MAPE], C64_MAP_KERNAL
    jne .e
    mov ax, [c64_rbk]
.e: push ax                         ; F+30 $E000-$FFFF
    mov ax, [_c64_m+CM_RAMSEG]
    cmp byte [es:C64_SCR_BASE + C64_SCR_MAPD], C64_MAP_RAM
    je .d
    xor ax, ax                      ; I/O or the CHARGEN: handed back
.d: push ax                         ; F+28 $D000-$DFFF
    mov ax, [_c64_m+CM_RAMSEG]
    cmp byte [es:C64_SCR_BASE + C64_SCR_MAPA], C64_MAP_BASIC
    jne .a
    mov ax, [c64_rbb]
.a: push ax                         ; F+26 $A000-$BFFF
    push word [_c64_m+CM_RAMSEG]    ; F+24 everything else
    push cs
    mov ax, c64f_iowr
    push ax
    push cs
    mov ax, c64f_iord
    push ax
    mov ax, [c64f_left]
    mov dx, [_c64_m+CM_RAMSEG]
    mov bx, _c64_m
    push ds
    pop es
    call far [c64f_far]             ; (its retf takes the frame words off)
    call .portout
    mov cx, [_c64_m+CM_CNT]
    mov [c64f_left], cx
    cmp ax, C64F_RSN_HB1
    jb .out                         ; the budget, spent (or cut)
    call .stop
    jc .slice
    cmp ax, C64F_RSN_HBR
    je .region

    ; --- A SHORT RUN ON THE ACCURATE CORE: one instruction, or the interrupt
    ; flag's few. The C64 has no CLKB: its devices advance on what a run
    ; SPENT (c64_advance), which the caller works out from cnt
    mov cx, 1
    cmp ax, C64F_RSN_HBI
    jne .short
    mov cx, C64F_HBI_RUN
    cmp cx, [c64f_left]
    jle .short
    mov cx, [c64f_left]
.short:
    mov [c64f_bud], cx
    mov ax, [c64f_left]             ; its countdown starts at CX, not at what
    sub ax, cx                      ; is left of the run: c64io.c's
    mov [_c64f_adj], ax             ; c64_line_rel takes the difference off
    push cx
    call c64_arun
    add sp, 2
    mov word [_c64f_adj], 0
    mov cx, [_c64_m+CM_CNT]         ; left -= budget - cnt
    add cx, [c64f_left]
    sub cx, [c64f_bud]
    mov [c64f_left], cx
    jmp short .back

.region:
    mov es, [_c64_m+CM_RAMSEG]
    or byte [es:C64F_PV], C64F_HBK
    push word [c64f_left]
    call c64_arun
    add sp, 2
    mov es, [_c64_m+CM_RAMSEG]
    and byte [es:C64F_PV], ~C64F_HBK
    mov cx, [_c64_m+CM_CNT]
    mov [c64f_left], cx

.back:
    cmp ax, C64_RUN_JAM
    je .out
    call .stop
    jnc .loop
.slice:
    xor ax, ax                      ; C64_RUN_SLICE
.out:
    mov cx, [c64f_left]
    mov [_c64_m+CM_CNT], cx
    mov es, [_c64_m+CM_RAMSEG]
    mov [es:C64_SCR_BASE + C64_SCR_DEAD], cx
    mov [_c64_m+CM_REASON], ax
    pop es
    pop di
    pop si
    pop bp
    ret

.stop:
    push es
    mov es, [_c64_m+CM_RAMSEG]
    test byte [es:C64F_PV], C64F_CUTF
    pop es
    jnz .yes
    cmp word [c64f_left], 0
    jle .yes
    clc
    ret
.yes:
    stc
    ret

; the processor port's read-back into $00/$01, the RAM there put aside
.portin:
    push ax
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov ax, [es:0]
    mov [c64f_ram01], ax
%ifdef C64F_PORT_VIA_IORD
    ; (c64cputest, whose port is its own bookkeeping: through its io_rd)
    xor ax, ax
    push ax
    call _c64_io_rd
    add sp, 2
    push ax
    mov ax, 1
    push ax
    call _c64_io_rd
    add sp, 2
    mov ah, al
    pop bx
    mov al, bl
    mov es, [_c64_m+CM_RAMSEG]
%else
    mov ax, [_c64_pp_rb]            ; $00 and $01's read-back, as c64io.c
%endif                              ; publishes them on every port write
    mov [es:0], ax
    pop es
    pop ax
    ret
.portout:
    push cx
    push es
    mov es, [_c64_m+CM_RAMSEG]
    mov cx, [c64f_ram01]
    mov [es:0], cx
    pop es
    pop cx
    ret

; -----------------------------------------------------------------------------
; c64f_iord - FAR, from the fast core: BX = an address in $D000-$DFFF -> DH.
; c64f_iowr - FAR: BX = the address, DH = the value.
; Every other register comes back - DS, the C64's RAM, included.
; -----------------------------------------------------------------------------
c64f_iord:
    push ds
    push es
    push ax
    push bx
    push cx
    push si
    push di
    push bp
    mov ds, [cs:c64_pkgds]
    ; THE 6502 PC, for c64io.c's raster wait (C64-SPEC 4.7.1) - but only
    ; from the fast core's own handlers, whose SI is the PC: the caller's CS,
    ; above eight pushes and the return offset, says whose call this is
    mov bp, sp
    xor cx, cx
    mov ax, [bp+18]
    cmp ax, [_c64_fseg]
    jne .nopc
    mov cx, si
.nopc:
    mov [_c64_fpc], cx
    push bx
    call _c64_io_rd
    add sp, 2
    mov word [_c64_fpc], 0          ; (no other caller may see it)
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

c64f_iowr:
    push ds
    push es
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    mov ds, [cs:c64_pkgds]
    mov al, dh
    xor ah, ah
    push ax
    push bx
    call _c64_io_wr
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
; void c64f_core(unsigned dstseg, unsigned partseg) - the 64KB code segment:
; the part's $FE00 bytes, and the $FE and $FF slots written as near jumps
; -----------------------------------------------------------------------------
_c64f_core:
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
    mov cx, C64F_PARTLEN / 2
    rep movsw                       ; cc8086:allow - DS, ES and DF set above
    mov ax, [C64F_FIXUP]
    sub ax, 0xFE03
    mov byte [es:0xFE00], 0xE9
    mov [es:0xFE01], ax
    mov ax, [C64F_FIXUP + 2]
    sub ax, 0xFF03
    mov byte [es:0xFF00], 0xE9
    mov [es:0xFF01], ax
    ; THE PAGE MAP, ZERO: no page goes to the recompiler until it says so
    ; (4.9.2) - the claim is not cleared, and $FF10 is past the part
    xor ax, ax
    mov di, C64F_PGMAP
    mov cx, 0xD0 / 2
    rep stosw                       ; cc8086:allow - ES and DF set above
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; -----------------------------------------------------------------------------
; void c64j_setup(unsigned dst, unsigned part, unsigned len, unsigned core,
;                 int force, int kb) - THE RECOMPILER (C64-SPEC 4.9): LEN bytes
; of its part into its KB-sized claim, and its jl_init far-called there with
; the fast core's segment, whose page map it writes
; -----------------------------------------------------------------------------
_c64j_setup:
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
    mov cx, [bp+8]
    shr cx, 1
    rep movsw                       ; cc8086:allow - DS, ES and DF set above
    mov ax, [bp+10]
    mov dl, [bp+12]
    mov dh, [bp+14]
    ; STKBALANCE-OK: a FAR CALL made by hand - CS and .back pushed as its
    ; return, ES and C64J_INIT as its target, and the retf goes there;
    ; jl_init's own retf comes back to .back with the four words gone
    push cs                         ; a far call through ES:C64J_INIT
    mov bx, .back
    push bx
    push es
    mov bx, C64J_INIT
    push bx
    retf
.back:
    pop es
    pop ds
    pop di
    pop si
    pop bp
    ret

; void c64j_off(unsigned core) - the fast core's page map zeroed: no RAM
; transfer goes to the recompiler again (C64-SPEC 4.9.2)
_c64j_off:
    push bp
    mov bp, sp
    push di
    push es
    cld
    mov es, [bp+4]
    mov di, C64F_PGMAP
    xor ax, ax
    mov cx, 0xD0 / 2
    rep stosw                       ; cc8086:allow - ES and DF set above
    pop es
    pop di
    pop bp
    ret

; -----------------------------------------------------------------------------
; THE D64 IN EXTENDED MEMORY (C64-SPEC 11.5.1): OSAPI_XMEM_* for the C, which
; has no 32-bit type - a block's token is two words, and one the C keeps in
; c64_xnew after an alloc
;   unsigned c64x_caps(void)                 the KB the pool can hand out
;   int  c64x_alloc(unsigned lo, unsigned hi) 1, and the token in c64_xnew
;   void c64x_free(unsigned tlo, unsigned thi)
;   int  c64x_copy(unsigned seg, unsigned off, unsigned tlo, unsigned thi,
;                  unsigned xlo, unsigned xhi, unsigned n, int in)
;        n bytes (even, <= 32768) between seg:off and the block's offset
;        xhi:xlo - in from extended when `in`, out to it otherwise; 1 done
; -----------------------------------------------------------------------------
_c64x_caps:
    push bx
    push si
    push di
    call OSAPI_XMEM_CAPS
    pop di
    pop si
    pop bx
    ret

_c64x_alloc:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov dx, [bp+6]
    call OSAPI_XMEM_ALLOC
    jc .no
    mov [_c64_xnew], ax
    mov [_c64_xnew+2], dx
    mov ax, 1
    jmp short .out
.no:
    xor ax, ax
.out:
    pop di
    pop si
    pop bp
    ret

_c64x_free:
    push bp
    mov bp, sp
    push si
    push di
    mov ax, [bp+4]
    mov dx, [bp+6]
    call OSAPI_XMEM_FREE
    pop di
    pop si
    pop bp
    ret

_c64x_copy:
    push bp
    mov bp, sp
    push si
    push di
    push es
    mov ax, [bp+8]
    mov dx, [bp+10]
    add ax, [bp+12]                 ; the token, plus the offset inside it
    adc dx, [bp+14]
    mov si, [bp+6]
    mov cx, [bp+16]
    mov di, [bp+18]
    mov es, [bp+4]
    call OSAPI_XMEM_COPY
    mov ax, 0                       ; (MOV leaves CF alone)
    jc .e
    inc ax
.e:
    pop es
    pop di
    pop si
    pop bp
    ret

; unsigned c64f_claimcode(void) - the fast core's 64KB code claim, 0 = refused.
; CONVENTIONAL on every kernel (MEMP_CONV, SPEC.md 50.3.7): on kern_mega that
; makes it an allocator pair whose code half OSAPI_MEM_CODESEL answers. C64 is
; OS88_HIGHMEM, so the KB door put its 64KB above 1MB there as a lone DATA
; selector, CODESEL refused it, and the fast core and both translated ROMs
; never ran on kern_mega (APPLE2's a2f_claimcode, the same fix).
_c64f_claimcode:
    mov ax, 0x1000                  ; 64KB in paragraphs
    xor cx, cx                      ; no head...
    mov bl, MEMP_CLAIM + MEMP_CONV  ; ...and conventional
    call OSAPI_MEM_P
    sbb ax, ax                      ; CF -> -1 / 0...
    not ax
    and ax, dx                      ; ...-> 0 = refused, or the claim
    ret

; unsigned c64f_codesel(unsigned seg) - OSAPI_MEM_CODESEL's answer, 0 = none
_c64f_codesel:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    call OSAPI_MEM_CODESEL
    jnc .ok
    xor ax, ax
.ok:
    pop bp
    ret
