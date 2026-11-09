; =============================================================================
; os8088 - apps/arcade/arc6502.inc - an NMOS 6502 interpreter, for Q*bert's
; sound board (Gottlieb rev. 1: an M6503 at 894,886 Hz, a 6532, a DAC)
;
; THE REFERENCE is tools/qbsnd.py's CPU6502, and this core matches it, not
; silicon, wherever the two could differ: the documented opcodes only, its
; decimal-mode ADC/SBC flag rules, JMP (ind)'s page wrap, its cycle counts
; (page crosses charged on indexed reads; a branch 2 cycles taken or not,
; because that is what qbsnd.py runs - M6_BR says why, and how to build the
; silicon count), IRQ and BRK as it pushes them, and ONE read and ONE write
; per read-modify-write (no dummy write).
; tests/m6502.py holds the two together, opcode by opcode and on the sound
; ROMs themselves, so a change here is a change to make there first.
;
; It runs LIVE (feeding the Sound Blaster stream), so the shape is for speed:
; every 6502 register lives in an 8086 register for the whole of m6_run, an
; opcode costs one masked byte fetch and one `jmp [bx+m6_tab]`, every handler
; is straight-line code that ends `sub di,<cycles>` / `jmp m6_next`, the IRQ
; line is looked at only where it can matter (m6_run), and the cycle total
; is settled once, at the way out, rather than per instruction.
;
; -----------------------------------------------------------------------------
; THE STATE THE INCLUDER DEFINES (in DS, the package's segment; bss is fine):
;   m6_a, m6_x, m6_y   byte    the registers
;   m6_s               byte    the stack pointer (the page is 01h)
;   m6_p               byte    P, in the 6502's own layout, as PHP pushes it
;   m6_irq             byte    the IRQ line: nonzero = asserted (level)
;   m6_pc              word    PC, the full 16 bits (A15 kept, see below)
;   m6_seg             word    the segment holding the 6502's memory image
;   m6_debt            word    cycles the last m6_run overran (see m6_run)
;   m6_bad             word    undocumented opcodes met (each a 2-cycle NOP)
;   m6_tgt             word    scratch: this call's cycle target
;   m6_left            word    scratch: cycles banked by m6_poll (m6_run)
;   m6_cyc             dword   total cycles run, ever (the includer may zero it)
;   m6_c0              dword   scratch: m6_cyc on the way in
;
; THE HOOKS THE INCLUDER PROVIDES (near procs, called with DS = the package,
; ES = [m6_seg], which they must give back):
;   m6_ior   BX = the address (A15 already off, in 0200-5FFFh) -> AL = the
;            byte. Preserves everything but AL.
;   m6_iow   BX = the address (as above), AL = the byte. Preserves everything.
; While a hook runs, m6_cyc holds the cycle count at the START of the
; instruction making the access - exactly what qbsnd.py's cpu.cyc reads
; during the access - so a 6532 timer can be read off it.
;
; THE MEMORY MAP. The board decodes A0-A14 only, so A15 is masked off every
; access that is not to zero page or the stack, fetches and vectors included
; (FFFC reads 7FFC). After the mask:
;   0000-01FF   plain bytes in [m6_seg] at the same offsets. Zero page and
;               the stack never reach a hook. (The real board mirrors the
;               6532's 128 bytes over 0000-01FF; this map does not.)
;   0200-5FFF   I/O: every read is m6_ior, every write m6_iow.
;   6000-7FFF   plain bytes (the ROM); a write there is dropped.
; Instruction bytes are fetched from the image directly, never through a
; hook: code must lie in 0000-01FF or 6000-7FFF, and an instruction may not
; straddle 01FF/0200 or 7FFF/8000 (the word fetch would not wrap the way the
; reference's two byte reads do). The sound ROM is code at 7000-7FF9.
;
; THE ENTRY POINTS
;   m6_reset   P = 24h (I set), S = FDh, PC = the word at 7FFCh, debt = 0.
;              A, X, Y, m6_cyc and m6_bad are left alone. Keeps registers.
;   m6_run     AX = a cycle budget, 1..32767. Runs whole instructions until
;              at least (AX - [m6_debt]) cycles are spent, then sets m6_debt
;              to the overrun, so a run of N calls of B cycles ends where one
;              of N*B would: the overrun of one call is taken off the next,
;              as qbsnd.Board's `while cyc < end` does with an absolute end.
;              A budget the debt swallows runs nothing and shrinks the debt.
;              Before each instruction an asserted [m6_irq] with I clear is
;              taken (7 cycles, counted as a step of its own). [m6_irq] is
;              read on the way in and after every I/O hook - set it before
;              the call or from inside m6_ior/m6_iow, not from an ISR mid-
;              run. Adds what it ran to m6_cyc. Keeps every register but
;              FLAGS.
;
; THE REGISTER MAP inside m6_run:
;   DL = A   CL = X   CH = Y   BP = 0100h + S   SI = PC   DI = cycles left
;   DH = P, with Z and V EXCHANGED (Z at bit 6, V at bit 1) so that LAHF's
;        S Z . . . . . C drops straight into N Z . . . . . C: an NZ update is
;        lahf / and / and / or with no table. m6_swap converts at PHP, PLP,
;        BRK, IRQ, RTI and at the way in and out.
;   ES = [m6_seg]; AX and BX are scratch. BP's high byte stays 01h because
;   the stack moves through BL (dec bl wraps in the page as S does).
; =============================================================================

; -----------------------------------------------------------------------------
; The pieces every handler is built of
; -----------------------------------------------------------------------------
%macro M6_NEXT 1                    ; the instruction cost %1 cycles
    sub di, %1
    jmp m6_next
%endmacro

%macro M6_NZ 0                      ; N Z from the last 8086 op's SF ZF
    lahf
    and ah, 0xC0
    and dh, 0x3F
    or dh, ah
%endmacro

%macro M6_NZC 0                     ; N Z C from SF ZF CF
    lahf
    and ah, 0xC1
    and dh, 0x3E
    or dh, ah
%endmacro

%macro M6_FB 0                      ; AL = the byte at PC, PC + 1
    mov bx, si
    and bh, 0x7F
    mov al, [es:bx]
    inc si
%endmacro

%macro M6_ZP 0                      ; BX = a zero-page operand (BH = 0)
    mov bx, si
    and bh, 0x7F
    mov bl, [es:bx]
    mov bh, 0
    inc si
%endmacro

%macro M6_AB 0                      ; BX = an absolute operand (A15 kept)
    mov bx, si
    and bh, 0x7F
    mov bx, [es:bx]
    inc si
    inc si
%endmacro

%macro M6_PTR 0                     ; BX = zero-page address -> the word there,
    cmp bl, 0xFF                    ;   its high byte from 00, not 0100
    jne %%n                         ;   (qbsnd.py: rd((z + 1) & 0xFF))
    mov bl, [es:0x00FF]
    mov bh, [es:0x0000]
    jmp short %%d
%%n:
    mov bx, [es:bx]
%%d:
%endmacro

%macro M6_RD 0                      ; AL = the byte at BX, through the map
    and bh, 0x7F
    cmp bh, 0x02
    jb %%m
    cmp bh, 0x60
    jae %%m
    call m6_rio
    jmp short %%d
%%m:
    mov al, [es:bx]
%%d:
%endmacro

%macro M6_WR 0                      ; AL to BX, through the map (ROM drops it)
    and bh, 0x7F
    cmp bh, 0x02
    jb %%m
    cmp bh, 0x60
    jae %%d
    call m6_wio
    jmp short %%d
%%m:
    mov [es:bx], al
%%d:
%endmacro

%macro M6_IDXR 1                    ; BX + %1 (CL or CH), then AL = the byte
    add bl, %1                      ;   there; a carry into the high byte is
    jc %%x                          ;   a page crossed, one cycle more - taken
    M6_RD                           ;   AFTER the read (m6_rdx), so a hook
    jmp short %%d                   ;   still sees the instruction's start
%%x:
    inc bh
    call m6_rdx
%%d:
%endmacro

; ADDR: the effective address of a mode in BX, no read and no penalty (the
; stores and the read-modify-writes: qbsnd.py charges them no page cross).
%macro M6_ADDR 1
%ifidni %1, zp
    M6_ZP
%elifidni %1, zpx
    M6_ZP
    add bl, cl
%elifidni %1, zpy
    M6_ZP
    add bl, ch
%elifidni %1, ab
    M6_AB
%elifidni %1, abx
    M6_AB
    add bl, cl
    adc bh, 0
%elifidni %1, aby
    M6_AB
    add bl, ch
    adc bh, 0
%elifidni %1, izx
    M6_ZP
    add bl, cl
    M6_PTR
%elifidni %1, izy
    M6_ZP
    M6_PTR
    add bl, ch
    adc bh, 0
%else
    %error unknown 6502 mode %1
%endif
%endmacro

; GET: AL = a mode's operand, the page-cross penalty included.
%macro M6_GET 1
%ifidni %1, imm
    M6_FB
%elifidni %1, zp
    M6_ADDR %1
    mov al, [es:bx]
%elifidni %1, zpx
    M6_ADDR %1
    mov al, [es:bx]
%elifidni %1, zpy
    M6_ADDR %1
    mov al, [es:bx]
%elifidni %1, ab
    M6_ADDR %1
    M6_RD
%elifidni %1, izx
    M6_ADDR %1
    M6_RD
%elifidni %1, abx
    M6_AB
    M6_IDXR cl
%elifidni %1, aby
    M6_AB
    M6_IDXR ch
%elifidni %1, izy
    M6_ZP
    M6_PTR
    M6_IDXR ch
%else
    %error unknown 6502 mode %1
%endif
%endmacro

%macro M6_LDM 1                     ; AL = the byte at BX, for mode %1's kind
%ifidni %1, zp
    mov al, [es:bx]
%elifidni %1, zpx
    mov al, [es:bx]
%else
    M6_RD
%endif
%endmacro

%macro M6_STM 1                     ; AL to BX, for mode %1's kind
%ifidni %1, zp
    mov [es:bx], al
%elifidni %1, zpx
    mov [es:bx], al
%elifidni %1, zpy
    mov [es:bx], al
%else
    M6_WR
%endif
%endmacro

%macro M6_PUSH 0                    ; AL onto the stack
    mov bx, bp
    mov [es:bx], al
    dec bl
    mov bp, bx
%endmacro

%macro M6_POP 0                     ; AL off the stack
    mov bx, bp
    inc bl
    mov al, [es:bx]
    mov bp, bx
%endmacro

; -----------------------------------------------------------------------------
; The operations on AL (the operand) - qbsnd.py's adc, sbc, cmp, bit, ...
; -----------------------------------------------------------------------------
%macro M6_BINADD 0                  ; A = A + AL + C, N Z C V (binary)
    mov ah, dh
    shr ah, 1                       ; CF = C
    adc dl, al
    mov al, 0
    jno %%v
    mov al, 0x02                    ; V, at its internal bit 1
%%v:
    lahf
    and ah, 0xC1
    or ah, al
    and dh, 0x3C
    or dh, ah
%endmacro

%macro M6_O_ADC 0
    test dh, 0x08
    jnz %%d
    M6_BINADD
    jmp short %%e
%%d:
    call m6_adcd
%%e:
%endmacro

%macro M6_O_SBC 0                   ; binary SBC is ADC of the complement,
    test dh, 0x08                   ;   as qbsnd.py has it
    jnz %%d
    not al
    M6_BINADD
    jmp short %%e
%%d:
    call m6_sbcd
%%e:
%endmacro

%macro M6_O_ORA 0
    or dl, al
    M6_NZ
%endmacro

%macro M6_O_AND 0
    and dl, al
    M6_NZ
%endmacro

%macro M6_O_EOR 0
    xor dl, al
    M6_NZ
%endmacro

%macro M6_O_CMP 0                   ; the 6502's C is NOT borrow: cmc
    cmp dl, al
    cmc
    M6_NZC
%endmacro

%macro M6_O_CPX 0
    cmp cl, al
    cmc
    M6_NZC
%endmacro

%macro M6_O_CPY 0
    cmp ch, al
    cmc
    M6_NZC
%endmacro

%macro M6_O_BIT 0                   ; N = M7, V = M6, Z = (A AND M) = 0
    and dh, 0x3D
    mov ah, al
    and ah, 0x80
    or dh, ah
    test al, 0x40
    jz %%1
    or dh, 0x02
%%1:
    test al, dl
    jnz %%2
    or dh, 0x40
%%2:
%endmacro

%macro M6_O_ASL 0
    shl al, 1
    M6_NZC
%endmacro

%macro M6_O_LSR 0
    shr al, 1
    M6_NZC
%endmacro

%macro M6_O_ROL 0                   ; adc al,al: the carry in, and S Z C out
    mov ah, dh
    shr ah, 1
    adc al, al
    M6_NZC
%endmacro

%macro M6_O_ROR 0                   ; rcr sets CF only, so S Z come after
    mov ah, dh
    shr ah, 1
    rcr al, 1
    mov ah, 0
    adc ah, 0
    and dh, 0x3E
    or dh, ah
    test al, al
    M6_NZ
%endmacro

%macro M6_O_INC 0
    inc al
    M6_NZ
%endmacro

%macro M6_O_DEC 0
    dec al
    M6_NZ
%endmacro

; -----------------------------------------------------------------------------
; The handler shapes: %1 = the opcode in hex, %2 = the mode, ... cycles last
; -----------------------------------------------------------------------------
%macro M6_LD 4                      ; LDA LDX LDY: %3 = the register
m6_x%1:
    M6_GET %2
    mov %3, al
    test al, al
    M6_NZ
    M6_NEXT %4
%endmacro

%macro M6_ST 4                      ; STA STX STY
m6_x%1:
    M6_ADDR %2
%ifidni %2, zp                      ; zero page: the register, straight in
    mov [es:bx], %3
%elifidni %2, zpx
    mov [es:bx], %3
%elifidni %2, zpy
    mov [es:bx], %3
%else
    mov al, %3
    M6_WR
%endif
    M6_NEXT %4
%endmacro

%macro M6_ALU 4                     ; %3 = the operation's macro
m6_x%1:
    M6_GET %2
    %3
    M6_NEXT %4
%endmacro

%macro M6_RMW 4                     ; one read, one write, as qbsnd.py does
m6_x%1:
    M6_ADDR %2
    M6_LDM %2
    %3
    M6_STM %2
    M6_NEXT %4
%endmacro

%macro M6_ACC 2                     ; ASL A, ROL A, LSR A, ROR A
m6_x%1:
    mov al, dl
    %2
    mov dl, al
    M6_NEXT 2
%endmacro

; A branch costs 2 cycles, 3 taken, 4 taken into another page - silicon's,
; and tools/qbsnd.py's since its `self.cyc += f()` was mended to keep the
; cycles branch() adds (it had read self.cyc before f() ran, and every branch
; cost 2: a DEX/BNE delay loop ran a quarter fast, and the pitch with it).
%macro M6_BR 3                      ; %2 = the flag (internal bit), %3 = the
m6_x%1:                             ;   jcc that means NOT taken
    test dh, %2
    %3 %%nt
    M6_FB
    cbw
    mov bx, si
    add si, ax
    xor bx, si
    neg bh                          ; CF = the target is in another page
    sbb di, 3                       ; 2 + 1 taken + 1 crossed
    jmp m6_next
%%nt:
    inc si
    M6_NEXT 2
%endmacro

%macro M6_IMP 2                     ; a one-line implied op, 2 cycles
m6_x%1:
    %2
    M6_NEXT 2
%endmacro

; =============================================================================
; m6_reset
; =============================================================================
m6_reset:
    push ax
    push es
    mov es, [m6_seg]
    mov ax, [es:0x7FFC]
    mov [m6_pc], ax
    mov byte [m6_p], 0x24
    mov byte [m6_s], 0xFD
    mov word [m6_debt], 0
    pop es
    pop ax
    ret

; =============================================================================
; m6_run - AX = the budget (above). The loop is m6_next .. `jmp [bx+m6_tab]`.
;
; THE IRQ IS NOT POLLED PER INSTRUCTION. "Line up and I clear" can only
; become true where the line or I can change: on the way in, after an I/O
; hook (the includer may move [m6_irq] there), and after CLI, PLP and RTI.
; Each of those calls m6_poll, which, when an IRQ is due, banks DI in
; m6_left and zeroes DI, so the instruction in hand ends the loop; m6_done
; takes the bank back and, with budget left, takes the IRQ and goes on. The
; IRQ lands before the next instruction exactly as qbsnd.py's step() would
; have it, and the 6 + 3 cycles of a `cmp [m6_irq]` per instruction are
; gone. (A [m6_irq] changed from OUTSIDE a run or a hook - an ISR - is seen
; at the next call.) m6_left is 0 outside that window.
; =============================================================================
m6_run:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
    push es
    mov di, ax
    sub di, [m6_debt]
    jg .go
    neg di                          ; the debt ate it: nothing runs
    mov [m6_debt], di
    jmp m6_ret
.go:
    mov [m6_tgt], di
    mov word [m6_left], 0
    mov ax, [m6_cyc]
    mov [m6_c0], ax
    mov ax, [m6_cyc+2]
    mov [m6_c0+2], ax
    mov es, [m6_seg]
    mov al, [m6_p]
    call m6_swap
    mov dh, al
    mov dl, [m6_a]
    mov cl, [m6_x]
    mov ch, [m6_y]
    mov al, [m6_s]
    mov ah, 0x01
    mov bp, ax
    mov si, [m6_pc]

m6_irqin:                           ; an IRQ due? (line up, I clear)
    test dh, 0x04
    jnz m6_fetch
    cmp byte [m6_irq], 0
    je m6_fetch
    mov ax, si                      ; PC hi, PC lo, P with B clear and bit 5
    mov bx, bp
    mov [es:bx], ah
    dec bl
    mov [es:bx], al
    dec bl
    mov bp, bx
    mov al, dh
    call m6_swap
    and al, 0xEF
    or al, 0x20
    M6_PUSH
    or dh, 0x04
    mov si, [es:0x7FFE]
    sub di, 7
m6_next:                            ; FLAGS = the last `sub di`
    jle m6_done
m6_fetch:
    mov bx, si
    and bh, 0x7F
    mov bl, [es:bx]
    mov bh, 0
    inc si
    shl bx, 1
    jmp [bx+m6_tab]

m6_done:
    add di, [m6_left]               ; m6_poll's bank, if it stopped us
    mov word [m6_left], 0
    jg m6_irqin                     ; budget left: it did, for an IRQ
    mov [m6_a], dl
    mov [m6_x], cl
    mov [m6_y], ch
    mov ax, bp
    mov [m6_s], al
    mov [m6_pc], si
    mov al, dh
    call m6_swap
    mov [m6_p], al
    mov ax, di                      ; DI <= 0: the overrun
    neg ax
    mov [m6_debt], ax
    call m6_now                     ; m6_cyc = m6_c0 + target - DI
m6_ret:
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
; m6_poll - an IRQ due (I clear, line up)? Then bank DI in m6_left and zero
; it, so the current instruction ends the loop (m6_run's comment). Keeps
; everything but FLAGS.
; -----------------------------------------------------------------------------
m6_poll:
    test dh, 0x04
    jnz .r
    cmp byte [m6_irq], 0
    je .r
    add [m6_left], di
    xor di, di
.r:
    ret

; -----------------------------------------------------------------------------
; m6_now - m6_cyc = m6_c0 + [m6_tgt] - DI - [m6_left]: the cycles at the
; start of the instruction in hand (DI only moves at its end, bar a page
; cross charged after its read). Keeps everything.
; -----------------------------------------------------------------------------
m6_now:
    push ax
    mov ax, [m6_tgt]
    sub ax, di
    sub ax, [m6_left]
    add ax, [m6_c0]
    mov [m6_cyc], ax
    mov ax, [m6_c0+2]
    adc ax, 0
    mov [m6_cyc+2], ax
    pop ax
    ret

m6_rio:                             ; a read in 0200-5FFF: settle the clock,
    call m6_now                     ;   the includer's hook, then the line
    call m6_ior
    jmp m6_poll

m6_wio:
    call m6_now
    call m6_iow
    jmp m6_poll

m6_rdx:                             ; M6_IDXR's page-crossed read
    M6_RD
    dec di
    ret

; -----------------------------------------------------------------------------
; m6_swap - AL = AL with bits 6 and 1 exchanged: P <-> the internal layout,
; either way. Clobbers AH.
; -----------------------------------------------------------------------------
m6_swap:
    mov ah, al
    and al, 0xBD
    test ah, 0x40
    jz .1
    or al, 0x02
.1:
    test ah, 0x02
    jz .2
    or al, 0x40
.2:
    ret

; -----------------------------------------------------------------------------
; m6_adcd / m6_sbcd - decimal ADC and SBC of AL, qbsnd.py's adc/sbc line for
; line (its N, V and Z rules are the NMOS ones: Z from the binary sum, N and
; V from the high digit before its +6). DL, DH in and out; AX, BX clobbered.
; -----------------------------------------------------------------------------
m6_adcd:
    push cx
    mov cl, al                      ; CL = v
    mov ah, dh
    shr ah, 1
    mov bl, dl
    adc bl, cl                      ; BL = (a + v + c) & FF, for Z
    mov ah, dh
    and ah, 0x01
    mov al, dl
    and al, 0x0F
    add ah, al
    mov al, cl
    and al, 0x0F
    add ah, al                      ; AH = lo
    mov al, dl
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    mov bh, cl
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    add bh, al                      ; BH = hi
    cmp ah, 9
    jbe .1
    add ah, 6
.1:
    cmp ah, 0x0F
    jbe .2
    inc bh
.2:
    mov ch, 0                       ; CH = the flags, internal layout
    or bl, bl
    jnz .3
    or ch, 0x40
.3:
    test bh, 0x08
    jz .4
    or ch, 0x80
.4:
    mov al, dl                      ; V = ~(a ^ v) & (a ^ (hi << 4)) & 80
    xor al, cl
    js .5
    mov al, bh
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    xor al, dl
    jns .5
    or ch, 0x02
.5:
    cmp bh, 9
    jbe .6
    add bh, 6
.6:
    cmp bh, 0x0F
    jbe .7
    or ch, 0x01
.7:
    mov al, bh
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    and ah, 0x0F
    or al, ah
    mov dl, al
    and dh, 0x3C
    or dh, ch
    pop cx
    ret

m6_sbcd:
    push cx
    mov cl, al                      ; CL = v
    mov ah, dh
    and ah, 0x01
    xor ah, 0x01                    ; AH = the borrow, 1 - c
    mov bl, dl
    and bl, 0x0F
    mov al, cl
    and al, 0x0F
    sub bl, al
    sub bl, ah                      ; BL = lo, signed
    mov bh, dl
    shr bh, 1
    shr bh, 1
    shr bh, 1
    shr bh, 1
    mov al, cl
    shr al, 1
    shr al, 1
    shr al, 1
    shr al, 1
    sub bh, al                      ; BH = hi, signed
    or bl, bl
    jns .1
    sub bl, 6
    dec bh
.1:
    or bh, bh
    jns .2
    sub bh, 6
.2:
    mov ah, dh                      ; the flags are the binary subtraction's:
    shr ah, 1                       ;   8086 SBB with CF = the borrow gives
    cmc                             ;   N Z V, and NOT C
    mov al, dl
    sbb al, cl
    mov ch, 0
    jno .3
    mov ch, 0x02
.3:
    cmc
    lahf
    and ah, 0xC1
    or ch, ah
    mov al, bh
    shl al, 1
    shl al, 1
    shl al, 1
    shl al, 1
    and bl, 0x0F
    or al, bl
    mov dl, al
    and dh, 0x3C
    or dh, ch
    pop cx
    ret

; =============================================================================
; The handlers, in qbsnd.py's order. Cycles are its table's.
; =============================================================================
    M6_LD A9, imm, dl, 2
    M6_LD A5, zp,  dl, 3
    M6_LD B5, zpx, dl, 4
    M6_LD AD, ab,  dl, 4
    M6_LD BD, abx, dl, 4
    M6_LD B9, aby, dl, 4
    M6_LD A1, izx, dl, 6
    M6_LD B1, izy, dl, 5
    M6_LD A2, imm, cl, 2
    M6_LD A6, zp,  cl, 3
    M6_LD B6, zpy, cl, 4
    M6_LD AE, ab,  cl, 4
    M6_LD BE, aby, cl, 4
    M6_LD A0, imm, ch, 2
    M6_LD A4, zp,  ch, 3
    M6_LD B4, zpx, ch, 4
    M6_LD AC, ab,  ch, 4
    M6_LD BC, abx, ch, 4

    M6_ST 85, zp,  dl, 3
    M6_ST 95, zpx, dl, 4
    M6_ST 8D, ab,  dl, 4
    M6_ST 9D, abx, dl, 5
    M6_ST 99, aby, dl, 5
    M6_ST 81, izx, dl, 6
    M6_ST 91, izy, dl, 6
    M6_ST 86, zp,  cl, 3
    M6_ST 96, zpy, cl, 4
    M6_ST 8E, ab,  cl, 4
    M6_ST 84, zp,  ch, 3
    M6_ST 94, zpx, ch, 4
    M6_ST 8C, ab,  ch, 4

%macro M6_ALU8 9                    ; one ALU op in its eight modes:
    M6_ALU %2, imm, %1, 2           ;   %1 = the op, %2.. = the opcodes
    M6_ALU %3, zp,  %1, 3           ;   imm zp zpx ab abx aby izx izy
    M6_ALU %4, zpx, %1, 4
    M6_ALU %5, ab,  %1, 4
    M6_ALU %6, abx, %1, 4
    M6_ALU %7, aby, %1, 4
    M6_ALU %8, izx, %1, 6
    M6_ALU %9, izy, %1, 5
%endmacro
    M6_ALU8 M6_O_ORA, 09, 05, 15, 0D, 1D, 19, 01, 11
    M6_ALU8 M6_O_AND, 29, 25, 35, 2D, 3D, 39, 21, 31
    M6_ALU8 M6_O_EOR, 49, 45, 55, 4D, 5D, 59, 41, 51
    M6_ALU8 M6_O_ADC, 69, 65, 75, 6D, 7D, 79, 61, 71
    M6_ALU8 M6_O_SBC, E9, E5, F5, ED, FD, F9, E1, F1
    M6_ALU8 M6_O_CMP, C9, C5, D5, CD, DD, D9, C1, D1

    M6_ALU E0, imm, M6_O_CPX, 2
    M6_ALU E4, zp,  M6_O_CPX, 3
    M6_ALU EC, ab,  M6_O_CPX, 4
    M6_ALU C0, imm, M6_O_CPY, 2
    M6_ALU C4, zp,  M6_O_CPY, 3
    M6_ALU CC, ab,  M6_O_CPY, 4
    M6_ALU 24, zp,  M6_O_BIT, 3
    M6_ALU 2C, ab,  M6_O_BIT, 4

%macro M6_RMW4 5                    ; zp zpx ab abx
    M6_RMW %2, zp,  %1, 5
    M6_RMW %3, zpx, %1, 6
    M6_RMW %4, ab,  %1, 6
    M6_RMW %5, abx, %1, 7
%endmacro
    M6_RMW4 M6_O_ASL, 06, 16, 0E, 1E
    M6_RMW4 M6_O_ROL, 26, 36, 2E, 3E
    M6_RMW4 M6_O_LSR, 46, 56, 4E, 5E
    M6_RMW4 M6_O_ROR, 66, 76, 6E, 7E
    M6_RMW4 M6_O_INC, E6, F6, EE, FE
    M6_RMW4 M6_O_DEC, C6, D6, CE, DE

    M6_ACC 0A, M6_O_ASL
    M6_ACC 2A, M6_O_ROL
    M6_ACC 4A, M6_O_LSR
    M6_ACC 6A, M6_O_ROR

m6_xAA:                             ; TAX
    mov cl, dl
    test cl, cl
    M6_NZ
    M6_NEXT 2
m6_xA8:                             ; TAY
    mov ch, dl
    test ch, ch
    M6_NZ
    M6_NEXT 2
m6_x8A:                             ; TXA
    mov dl, cl
    test dl, dl
    M6_NZ
    M6_NEXT 2
m6_x98:                             ; TYA
    mov dl, ch
    test dl, dl
    M6_NZ
    M6_NEXT 2
m6_xBA:                             ; TSX
    mov ax, bp
    mov cl, al
    test cl, cl
    M6_NZ
    M6_NEXT 2
m6_x9A:                             ; TXS (no flags)
    mov ax, bp
    mov al, cl
    mov bp, ax
    M6_NEXT 2
m6_xE8:                             ; INX
    inc cl
    M6_NZ
    M6_NEXT 2
m6_xC8:                             ; INY
    inc ch
    M6_NZ
    M6_NEXT 2
m6_xCA:                             ; DEX
    dec cl
    M6_NZ
    M6_NEXT 2
m6_x88:                             ; DEY
    dec ch
    M6_NZ
    M6_NEXT 2
m6_xEA:                             ; NOP
    M6_NEXT 2
    M6_IMP 18, {and dh, 0xFE}       ; CLC
    M6_IMP 38, {or dh, 0x01}        ; SEC
    M6_IMP 78, {or dh, 0x04}        ; SEI
    M6_IMP B8, {and dh, 0xFD}       ; CLV (V is internal bit 1)
    M6_IMP D8, {and dh, 0xF7}       ; CLD
    M6_IMP F8, {or dh, 0x08}        ; SED

m6_x48:                             ; PHA
    mov al, dl
    M6_PUSH
    M6_NEXT 3
m6_x08:                             ; PHP: B and bit 5 set
    mov al, dh
    call m6_swap
    or al, 0x30
    M6_PUSH
    M6_NEXT 3
m6_x68:                             ; PLA
    M6_POP
    mov dl, al
    test al, al
    M6_NZ
    M6_NEXT 4
m6_x28:                             ; PLP: B clear, bit 5 set
    M6_POP
    and al, 0xEF
    or al, 0x20
    call m6_swap
    mov dh, al
    call m6_poll                    ; (I may have cleared)
    M6_NEXT 4
m6_x58:                             ; CLI
    and dh, 0xFB
    call m6_poll
    M6_NEXT 2

    M6_BR 10, 0x80, jnz             ; BPL
    M6_BR 30, 0x80, jz              ; BMI
    M6_BR 50, 0x02, jnz             ; BVC
    M6_BR 70, 0x02, jz              ; BVS
    M6_BR 90, 0x01, jnz             ; BCC
    M6_BR B0, 0x01, jz              ; BCS
    M6_BR D0, 0x40, jnz             ; BNE
    M6_BR F0, 0x40, jz              ; BEQ

m6_x4C:                             ; JMP abs
    mov bx, si
    and bh, 0x7F
    mov si, [es:bx]
    M6_NEXT 3
m6_x6C:                             ; JMP (ind): the high byte from the SAME
    mov bx, si                      ;   page, (a & FF00) | ((a + 1) & FF)
    and bh, 0x7F
    mov bx, [es:bx]
    M6_RD                           ; (keeps AH, and BX but its A15)
    mov ah, al
    inc bl
    M6_RD
    xchg al, ah
    mov si, ax
    M6_NEXT 5
m6_x20:                             ; JSR: pushes the address of its own
    mov bx, si                      ;   last byte
    and bh, 0x7F
    mov ax, [es:bx]
    inc si
    xchg ax, si
    mov bx, bp
    mov [es:bx], ah
    dec bl
    mov [es:bx], al
    dec bl
    mov bp, bx
    M6_NEXT 6
m6_x60:                             ; RTS
    mov bx, bp
    inc bl
    mov al, [es:bx]
    inc bl
    mov ah, [es:bx]
    mov bp, bx
    inc ax
    mov si, ax
    M6_NEXT 6
m6_x40:                             ; RTI
    M6_POP
    and al, 0xEF
    or al, 0x20
    call m6_swap
    mov dh, al
    mov bx, bp
    inc bl
    mov al, [es:bx]
    inc bl
    mov ah, [es:bx]
    mov bp, bx
    mov si, ax
    call m6_poll                    ; (I may have cleared)
    M6_NEXT 6
m6_x00:                             ; BRK: skips its padding byte, pushes
    inc si                          ;   P with B set, vectors through FFFE
    mov ax, si
    mov bx, bp
    mov [es:bx], ah
    dec bl
    mov [es:bx], al
    dec bl
    mov bp, bx
    mov al, dh
    call m6_swap
    or al, 0x30
    M6_PUSH
    or dh, 0x04
    mov si, [es:0x7FFE]
    M6_NEXT 7

m6_undoc:                           ; not in qbsnd.py's table: a one-byte,
    inc word [m6_bad]               ;   two-cycle NOP, counted
    M6_NEXT 2

; -----------------------------------------------------------------------------
; m6_tab - the dispatch: a handler per opcode
; -----------------------------------------------------------------------------
m6_tab:
    dw m6_x00, m6_x01, m6_undoc, m6_undoc, m6_undoc, m6_x05, m6_x06, m6_undoc ; 00
    dw m6_x08, m6_x09, m6_x0A, m6_undoc, m6_undoc, m6_x0D, m6_x0E, m6_undoc ; 08
    dw m6_x10, m6_x11, m6_undoc, m6_undoc, m6_undoc, m6_x15, m6_x16, m6_undoc ; 10
    dw m6_x18, m6_x19, m6_undoc, m6_undoc, m6_undoc, m6_x1D, m6_x1E, m6_undoc ; 18
    dw m6_x20, m6_x21, m6_undoc, m6_undoc, m6_x24, m6_x25, m6_x26, m6_undoc ; 20
    dw m6_x28, m6_x29, m6_x2A, m6_undoc, m6_x2C, m6_x2D, m6_x2E, m6_undoc ; 28
    dw m6_x30, m6_x31, m6_undoc, m6_undoc, m6_undoc, m6_x35, m6_x36, m6_undoc ; 30
    dw m6_x38, m6_x39, m6_undoc, m6_undoc, m6_undoc, m6_x3D, m6_x3E, m6_undoc ; 38
    dw m6_x40, m6_x41, m6_undoc, m6_undoc, m6_undoc, m6_x45, m6_x46, m6_undoc ; 40
    dw m6_x48, m6_x49, m6_x4A, m6_undoc, m6_x4C, m6_x4D, m6_x4E, m6_undoc ; 48
    dw m6_x50, m6_x51, m6_undoc, m6_undoc, m6_undoc, m6_x55, m6_x56, m6_undoc ; 50
    dw m6_x58, m6_x59, m6_undoc, m6_undoc, m6_undoc, m6_x5D, m6_x5E, m6_undoc ; 58
    dw m6_x60, m6_x61, m6_undoc, m6_undoc, m6_undoc, m6_x65, m6_x66, m6_undoc ; 60
    dw m6_x68, m6_x69, m6_x6A, m6_undoc, m6_x6C, m6_x6D, m6_x6E, m6_undoc ; 68
    dw m6_x70, m6_x71, m6_undoc, m6_undoc, m6_undoc, m6_x75, m6_x76, m6_undoc ; 70
    dw m6_x78, m6_x79, m6_undoc, m6_undoc, m6_undoc, m6_x7D, m6_x7E, m6_undoc ; 78
    dw m6_undoc, m6_x81, m6_undoc, m6_undoc, m6_x84, m6_x85, m6_x86, m6_undoc ; 80
    dw m6_x88, m6_undoc, m6_x8A, m6_undoc, m6_x8C, m6_x8D, m6_x8E, m6_undoc ; 88
    dw m6_x90, m6_x91, m6_undoc, m6_undoc, m6_x94, m6_x95, m6_x96, m6_undoc ; 90
    dw m6_x98, m6_x99, m6_x9A, m6_undoc, m6_undoc, m6_x9D, m6_undoc, m6_undoc ; 98
    dw m6_xA0, m6_xA1, m6_xA2, m6_undoc, m6_xA4, m6_xA5, m6_xA6, m6_undoc ; A0
    dw m6_xA8, m6_xA9, m6_xAA, m6_undoc, m6_xAC, m6_xAD, m6_xAE, m6_undoc ; A8
    dw m6_xB0, m6_xB1, m6_undoc, m6_undoc, m6_xB4, m6_xB5, m6_xB6, m6_undoc ; B0
    dw m6_xB8, m6_xB9, m6_xBA, m6_undoc, m6_xBC, m6_xBD, m6_xBE, m6_undoc ; B8
    dw m6_xC0, m6_xC1, m6_undoc, m6_undoc, m6_xC4, m6_xC5, m6_xC6, m6_undoc ; C0
    dw m6_xC8, m6_xC9, m6_xCA, m6_undoc, m6_xCC, m6_xCD, m6_xCE, m6_undoc ; C8
    dw m6_xD0, m6_xD1, m6_undoc, m6_undoc, m6_undoc, m6_xD5, m6_xD6, m6_undoc ; D0
    dw m6_xD8, m6_xD9, m6_undoc, m6_undoc, m6_undoc, m6_xDD, m6_xDE, m6_undoc ; D8
    dw m6_xE0, m6_xE1, m6_undoc, m6_undoc, m6_xE4, m6_xE5, m6_xE6, m6_undoc ; E0
    dw m6_xE8, m6_xE9, m6_xEA, m6_undoc, m6_xEC, m6_xED, m6_xEE, m6_undoc ; E8
    dw m6_xF0, m6_xF1, m6_undoc, m6_undoc, m6_undoc, m6_xF5, m6_xF6, m6_undoc ; F0
    dw m6_xF8, m6_xF9, m6_undoc, m6_undoc, m6_undoc, m6_xFD, m6_xFE, m6_undoc ; F8
