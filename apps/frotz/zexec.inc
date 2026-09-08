; =============================================================================
; os8088 - apps/frotz/zexec.inc
;
; The Z-machine itself: instruction decode (Standard 4), the call frames and
; the Z-stack (Standard 6), and every opcode in the table (Standard 15). Every
; other module in Frotz exists to be called from here.
;
; THREE DECISIONS SHAPE THIS FILE.
;
;  - **The PC is a live ES:SI and stays one.** docs/plans/completed/FROTZ-PLAN.md 3.1: a story
;    address is 20 bits, a claim hands back a base segment, so an instruction
;    fetch is `mov al, [es:si] / inc si` and a branch is an `add si`. It is
;    spilled to [zf_pcseg]/[zf_pcoff] only where something else must own ES -
;    a kernel call, the Z-stack, an input wait - and reloaded straight after.
;    zm_norm renormalises once per instruction, which is once per 32KB of
;    straight-line code, not once per fetch.
;
;  - **The lock is held by the burst, not by the opcode.** Output goes through
;    zt_*/zw_*, which draw, so the gfx lock has to be held while an opcode
;    runs. Holding it for a whole turn would freeze the desktop for seconds
;    (SPEC.md 20.6 rule 3), so zx_run takes it, runs ZX_BURST instructions,
;    and drops it. The opcodes that WAIT - @read, @read_char, @save, @restore,
;    @verify - drop it themselves around the wait and take it back after,
;    because a worker holding the lock while it waits for a keystroke is a
;    hung machine, not a slow one.
;
;  - **Every byte read from the story is hostile.** An opcode number outside
;    its table, a routine claiming 200 locals, a stack that underflows, a
;    divide by zero: each sets [zf_err] and parks the VM with a message
;    (SPEC.md 47 - say the reason), and none of them executes wild or faults
;    the 8086. There is no int 0 handler in this kernel, so `idiv` is guarded
;    before the fact rather than trapped after it.
;
; ZX_BURST is 64 and the arithmetic is PERFORMANCE.md's. A system tick is
; 18.2 Hz = 55ms, and a lock hold longer than that shows as a stalled clock
; and a stuttering mouse. An average Z-instruction here is a decode, two or
; three near calls at 11us and a couple of zm_gb16 at ~30us: call it 400us on
; a 4.77MHz 8088. 64 x 400us = 26ms, half a tick. The pathological burst is 64
; consecutive @print_char, each ~900us for its glyph cell, = 58ms - one tick,
; which is the worst case and is still bounded. 128 would double both numbers
; and buy nothing measurable, because the cost of dropping and retaking the
; lock is one OSAPI far call pair, 93us, against 26ms of work.
; =============================================================================

; --- error codes -------------------------------------------------------------
; These live at 0x40 upwards so they cannot collide with zmem.inc's ZERR_*,
; which are written to the same [zf_err] byte. zx_report treats anything
; outside our range as somebody else's code and prints the generic line.
ZXE_BASE    equ 0x40
ZXE_BADOP   equ ZXE_BASE + 0        ; an opcode number with no opcode
ZXE_DIV0    equ ZXE_BASE + 1        ; @div or @mod by zero (Standard 15)
ZXE_USTACK  equ ZXE_BASE + 2        ; the evaluation stack underflowed
ZXE_OSTACK  equ ZXE_BASE + 3        ; ...or ran off the end of its claim
ZXE_LOCALS  equ ZXE_BASE + 4        ; a routine header claiming > 15 locals
ZXE_RET     equ ZXE_BASE + 5        ; a return from the outermost frame
ZXE_FRAME   equ ZXE_BASE + 6        ; @throw to a frame not on the chain
ZXE_COUNT   equ 7

ZX_BURST    equ 64                  ; instructions per lock hold - see the banner

; The Z-stack claim. zio.inc makes it at load time; this is the size zexec
; will not write past, and the two have to agree (16KB = 8192 words, well
; over the Standard's 1024-word minimum in 6.3.3 and enough for the modern
; stories the disk carries).
ZX_STKBYTES equ ZI_STKKB * 1024

; What a halt reports. A %define expands at the use site but NASM still reads
; this file top to bottom, so these have to precede zx_bad.
%define zx_raw     (os88_image_end + ZEXEC_SLACK + 412)  ; byte: the opcode BYTE
%define zx_seglim  (os88_image_end + ZEXEC_SLACK + 414)  ; word: last valid ES

; --- call frame layout, in the [zf_stkseg] claim ------------------------------
; One frame, then its locals, then its evaluation stack. Standard 6.3.1/6.3.2:
; the evaluation stack is empty at routine entry and thrown away at return, so
; "return" is `zf_sp = zf_fp` and nothing else needs unwinding.
ZXF_PREV    equ 0                   ; word: the caller's frame base (0xFFFF = none)
ZXF_PCL     equ 2                   ; word: return address, low 16 bits
ZXF_PCH     equ 4                   ; word: ...and high 4
ZXF_STORE   equ 6                   ; byte: where the result goes (0xFF = discard)
ZXF_NLOC    equ 7                   ; byte: locals in this frame
ZXF_ARGC    equ 8                   ; word: arguments supplied (@check_arg_count)
ZXF_LOC     equ 10                  ; the locals start here

; --- private bss, out of ZEXEC_SLACK (512 bytes; 82 used) --------------------
%define zx_op       (os88_image_end + ZEXEC_SLACK + 0)   ; byte: opcode number
%define zx_booted   (os88_image_end + ZEXEC_SLACK + 1)   ; byte: zx_boot has run
%define zx_seen     (os88_image_end + ZEXEC_SLACK + 2)   ; byte: last [zf_state]
%define zx_msg      (os88_image_end + ZEXEC_SLACK + 4)   ; word: the halt message
%define zx_ra_l     (os88_image_end + ZEXEC_SLACK + 6)   ; word: routine address
%define zx_ra_h     (os88_image_end + ZEXEC_SLACK + 8)   ; word
%define zx_rp_l     (os88_image_end + ZEXEC_SLACK + 10)  ; word: return address
%define zx_rp_h     (os88_image_end + ZEXEC_SLACK + 12)  ; word
%define zx_tmp0     (os88_image_end + ZEXEC_SLACK + 14)  ; word: scratch, callee
%define zx_tmp1     (os88_image_end + ZEXEC_SLACK + 16)  ;       owned, never
%define zx_tmp2     (os88_image_end + ZEXEC_SLACK + 18)  ;       live across a
%define zx_tmp3     (os88_image_end + ZEXEC_SLACK + 20)  ;       call out
%define zx_sum      (os88_image_end + ZEXEC_SLACK + 22)  ; word: @verify total
%define zx_encbuf   (os88_image_end + ZEXEC_SLACK + 24)  ; 40: @encode_text in
%define zx_encout   (os88_image_end + ZEXEC_SLACK + 64)  ; 12: ...and out

; =============================================================================
; zx_worker - the VM task entry (SPEC.md 20.6)
; in:  DX = our instance index, DS = CS = our segment, ES = KERNEL_SEG,
;      SS = LOW_SEG, IF = 1, the gfx lock NOT held
; out: never returns. The only way out is OSAPI_TASK_ALIVE not coming back,
;      which is how the kernel reaps us when the window closes.
;
; The outer loop in the order the contract demands: ALIVE first and with the
; lock free (rule 4 - gfx_lock is not reentrant and ALIVE under it deadlocks
; the machine), then one bounded burst, then a yield. zx_run owns the lock for
; the length of the burst and nothing outside it holds the lock at all.
;
; The boot edge is detected here rather than published as an entry point zio
; must remember to call: [zx_seen] is the last [zf_state] this loop saw, so a
; story arriving (ZFS_LOAD -> ZFS_RUN) boots the machine, and a second story
; loaded over the first boots it again. All-zero is a valid start because
; ZFS_NONE is 0 and 0 != ZFS_RUN.
; =============================================================================
zx_worker:
.top:
    mov bx, [zf_win]
    call OSAPI_TASK_ALIVE           ; may never return; preserves everything
    mov al, [zf_state]
    cmp al, ZFS_RUN
    jne .idle
    cmp byte [zx_seen], ZFS_RUN
    je .running
    call zx_boot
    mov byte [zx_seen], ZFS_RUN
.running:
    cmp byte [zf_err], 0
    jne .fatal
    cmp byte [zf_dead], 0
    jne .idle
    mov cx, ZX_BURST
    call zx_run
    call OSAPI_TASK_YIELD
    jmp .top
.idle:
    mov al, [zf_state]
    mov [zx_seen], al               ; so the next ZFS_RUN is an edge again
    mov ax, 2
    call OSAPI_TASK_SLEEP           ; ~110ms; nothing to do costs nothing
    jmp .top
.fatal:
    call zx_report                  ; say why, once, then park
%ifdef ZHARNESS
    mov si, zh_m_halt               ; ...and tell the host, which is running
    call zh_mark                    ; unattended and cannot read the screen
%endif
    mov byte [zf_dead], 1
    mov byte [zf_err], 0
    jmp .top

; -----------------------------------------------------------------------------
; zx_boot - bring the machine up on the story zio has just loaded
; in:  [zf_*] header cache valid, [zf_stkseg] claimed; the lock NOT held
; out: nothing; preserves all registers
;
; Standard 6.1/6.4: v6 begins by CALLING the packed routine in header word 6;
; every other version begins executing at that byte address. The dummy frame
; at offset 0 exists so that a @ret from the outermost routine is a diagnosis
; rather than a wild jump - its ZXF_PREV is 0xFFFF and zx_ret tests for it.
; -----------------------------------------------------------------------------
zx_boot:
    push ax
    push bx
    push cx
    push dx
    push si
    push es

    push es
    mov es, [zf_stkseg]
    xor bx, bx
    mov word [es:bx+ZXF_PREV], 0xFFFF
    mov word [es:bx+ZXF_PCL], 0
    mov word [es:bx+ZXF_PCH], 0
    mov byte [es:bx+ZXF_STORE], 0xFF
    mov byte [es:bx+ZXF_NLOC], 0
    mov word [es:bx+ZXF_ARGC], 0
    pop es

    ; The highest segment the PC may legally hold: base + (length >> 4). A word
    ; may start at the last byte, so the limit is inclusive and generous by a
    ; paragraph - this is a sanity net, not a bounds check on reads (zmem does
    ; those).
    mov ax, [zf_slen_l]
    mov dx, [zf_slen_h]
    mov cl, 4
    shr ax, cl
    mov bx, dx
    mov cl, 12
    shl bx, cl
    or ax, bx
    add ax, [zf_sseg]
    mov [zx_seglim], ax

    mov word [zf_fp], 0
    mov word [zf_sp], ZXF_LOC
    mov byte [zf_nlocals], 0
    mov byte [zf_dead], 0
    mov byte [zf_err], 0
    mov word [zf_rndseq], 0         ; Standard 2.4: "random" at every restart
    mov word [zf_rndctr], 0
    call zx_seedclock

    cmp byte [zf_ver], 6
    je .v6
    xor dx, dx
    mov ax, [zf_initpc]
    call zm_seek
    jmp short .done
.v6:
    xor dx, dx                      ; a valid ES:SI first: zx_call saves the
    xor ax, ax                      ; current PC as main's return address, and
    call zm_seek                    ; 0:0 is the "main returned" sentinel
    mov ax, [zf_initpc]
    mov bl, 0
    call zm_unpack                  ; DX:AX = the main routine's byte address
    xor cx, cx
    mov bl, 0xFF
    call zx_call
.done:
    call zx_spill
%ifdef ZXTRACE
    call zx_diag
%endif
    mov byte [zx_booted], 1
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zx_seedclock - put the generator into "random" state (Standard 2.4)
; in:  nothing.  out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zx_seedclock:
    push ax
    push si
    push es
    call OSAPI_GET_TICKS            ; out AX = [ticks]
    pop es
    pop si
    or ax, ax
    jnz .ok
    mov ax, 0x2953                  ; the xorshift below has no zero state
.ok:
    mov [zf_rndlo], ax
    mov word [zf_rndhi], 0
    pop ax
    ret

; =============================================================================
; zx_run - one bounded burst of interpretation
; in:  CX = the instruction budget; the gfx lock NOT held
; out: nothing; preserves all registers. The PC is left spilled in
;      [zf_pcseg]/[zf_pcoff], which is where it lives between bursts.
;
; The lock is taken here and dropped here, so every opcode body may draw
; without asking, and no opcode body may take the lock again.
; =============================================================================
zx_run:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    push es
    call zx_load                    ; ES:SI = the PC
    call zx_lock
    jcxz .stop
.next:
    mov es, [zf_pcseg]              ; THE SEGMENT COMES FROM MEMORY, EVERY
                                    ; INSTRUCTION. The offset is fine in SI -
                                    ; an OSAPI slot preserves it - but ES is
                                    ; the one register the OS does not preserve,
                                    ; so a handler that draws or reads a file
                                    ; could lose the program counter and the
                                    ; interpreter would carry on executing a
                                    ; font. Asking sixty handlers to preserve
                                    ; ES was losing whack-a-mole; this makes it
                                    ; structurally impossible.
    push cx
    call zx_step
    pop cx
    mov [zf_pcoff], si              ; ...and the offset goes back, so a jump
                                    ; taken inside the handler survives too
    cmp byte [zf_err], 0
    jne .stop
    cmp byte [zf_dead], 0
    jne .stop
    dec cx
    jnz .next
.stop:
    call zx_unlock
    call zx_spill
    pop es
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zx_lock / zx_unlock - the gfx lock, without losing the PC
; in:  ES:SI = the PC.  out: unchanged.
;
; An OSAPI slot preserves DS and nothing else by contract, and ES:SI is the
; program counter, so it is pushed rather than trusted (SPEC.md 20.5).
; -----------------------------------------------------------------------------
zx_lock:
    push si
    push es
    call OSAPI_GFX_LOCK
    pop es
    pop si
    ret
zx_unlock:
    push si
    push es
    call OSAPI_GFX_UNLOCK
    pop es
    pop si
    ret

; -----------------------------------------------------------------------------
; zx_spill / zx_load - move the PC between ES:SI and the bss
; in/out: as named; preserve every other register
; -----------------------------------------------------------------------------
zx_spill:
    mov [zf_pcoff], si              ; the OFFSET only. [zf_pcseg] is written by
    ret                             ; the PC movers and by nobody else - ES at
                                    ; this point may be whatever the last OSAPI
                                    ; call left behind, which is exactly the
                                    ; thing this arrangement stops trusting.
zx_load:
    mov es, [zf_pcseg]
    mov si, [zf_pcoff]
    ret

; =============================================================================
; zx_step - decode and execute exactly one instruction (Standard 4)
; in:  ES:SI = the PC, the gfx lock held
; out: the PC advanced past the instruction; preserves nothing
;
; The four forms, tested in the order the Standard states them: extended
; (opcode 190 in v5+, a second opcode byte follows), variable (top two bits
; 11), short (10), long (anything else). Each form leaves the opcode number in
; [zx_op], the operands in [zf_ops] with their count in [zf_opn], and jumps
; through the table for its operand count. The handler runs with the PC
; pointing at the store byte / branch data / inline text, which is exactly
; where zx_storeres, zx_branch and zt_print_ret each expect to find theirs.
; =============================================================================
zx_step:
    ; THE PC MUST STILL BE INSIDE THE STORY. ES is the program counter's
    ; segment (docs/plans/completed/FROTZ-PLAN.md 3.1), so an opcode handler that clobbers ES
    ; and does not put it back does not crash - it silently starts executing
    ; whatever is at the same offset in some other claim, and the story dies
    ; hundreds of instructions later somewhere unrelated. One compare catches
    ; it at the instruction after the one that did it, and the halt names that
    ; opcode. It cost about 20 bytes and it found two bugs.
    mov ax, es
    cmp ax, [zf_sseg]
    jb zx_lost
    cmp ax, [zx_seglim]
    ja zx_lost
    cmp si, 0x8000
    jb .norm
    call zm_norm                    ; once per 32KB of straight-line code
.norm:
    mov byte [zf_opn], 0
    mov byte [zf_store], 0xFF
%ifdef ZXTRACE
    call zx_trace                   ; ES:SI is the PC; prints and preserves all
%endif
    mov al, [es:si]
    inc si
    mov [zx_raw], al                ; THE BYTE, not the number the forms mask
                                    ; out of it. A halt that says "opcode 5"
                                    ; names five different instructions - 2OP
                                    ; @inc_chk, 1OP @inc, 0OP @save, VAR
                                    ; @print_char, EXT @draw_picture - and the
                                    ; byte is the only thing that separates
                                    ; them. One store per instruction buys the
                                    ; form, and tools/zharness.py names it.

    cmp al, 0xBE
    jne .notext
    cmp byte [zf_ver], 5
    jb .notext
    ; --- extended form (Standard 4.3.4): VAR operands, opcode in a 2nd byte --
    mov al, [es:si]
    inc si
    mov [zx_op], al
    call zx_types1
    call zx_getops
    mov bl, [zx_op]
    cmp bl, 32
    jae zx_bad
    xor bh, bh
    shl bx, 1
    jmp word [zx_text + bx]

.notext:
    cmp al, 0xC0
    jb .shortform
    ; --- variable form (Standard 4.3.3): bit 5 picks VAR from 2OP -----------
    mov ah, al
    and al, 0x1F
    mov [zx_op], al
    test ah, 0x20
    jz .var2op
    cmp al, 12                      ; @call_vs2 and @call_vn2 alone take a
    je .dbl                         ; second type byte (Standard 4.4.3.1)
    cmp al, 26
    je .dbl
    call zx_types1
    jmp short .varops
.dbl:
    call zx_types1
    call zx_types1                  ; appends at [zf_opn], so 8 types in all
.varops:
    call zx_getops
    mov bl, [zx_op]
    xor bh, bh
    shl bx, 1
    jmp word [zx_tvar + bx]
.var2op:
    call zx_types1
    call zx_getops
    jmp short .do2op

.shortform:
    cmp al, 0x80
    jb .longform
    ; --- short form (Standard 4.3.1): bits 4-5 are the operand's type -------
    mov ah, al
    and al, 0x0F
    mov [zx_op], al
    mov al, ah
    mov cl, 4
    shr al, cl
    and al, 3
    cmp al, 3
    je .zeroop                      ; type "omitted" means 0OP
    mov [zf_optype], al
    mov byte [zf_opn], 1
    call zx_getops
    mov bl, [zx_op]
    xor bh, bh
    shl bx, 1
    jmp word [zx_t1op + bx]
.zeroop:
    mov bl, [zx_op]
    xor bh, bh
    shl bx, 1
    jmp word [zx_t0op + bx]

.longform:
    ; --- long form (Standard 4.3.2/4.4.2): always 2OP, one bit per type -----
    mov ah, al
    and al, 0x1F
    mov [zx_op], al
    mov byte [zf_opn], 2
    mov byte [zf_optype], 1         ; 1 = small constant
    test ah, 0x40
    jz .t1
    mov byte [zf_optype], 2         ; 2 = variable
.t1:
    mov byte [zf_optype+1], 1
    test ah, 0x20
    jz .t2
    mov byte [zf_optype+1], 2
.t2:
    call zx_getops
.do2op:
    mov bl, [zx_op]
    cmp bl, 32
    jae zx_bad
    xor bh, bh
    shl bx, 1
    jmp word [zx_t2op + bx]

; -----------------------------------------------------------------------------
; zx_types1 - consume one operand-type byte and append its types
; in:  ES:SI = the PC, [zf_opn] = types collected so far
; out: [zf_optype]/[zf_opn] extended; preserves all registers
;
; Standard 4.4.3: four 2-bit fields, most significant first, and once one is
; "omitted" every later one must be - so the first omitted ends the byte. The
; `rol dl,1` pair walks the byte two bits at a time and leaves it as it found
; it, which is cheaper than a shift by CL inside a loop that is using CX.
; -----------------------------------------------------------------------------
zx_types1:
    push ax
    push bx
    push cx
    push dx
    mov dl, [es:si]
    inc si
    mov bl, [zf_opn]
    xor bh, bh
    mov cx, 4
.l:
    rol dl, 1
    rol dl, 1
    mov al, dl
    and al, 3
    cmp al, 3
    je .done
    cmp bl, 8                       ; no instruction has more than 8 (4.5.1)
    jae .done
    mov [zf_optype+bx], al
    inc bx
    loop .l
.done:
    mov [zf_opn], bl
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zx_getops - read [zf_opn] operands into [zf_ops] (Standard 4.5)
; in:  ES:SI = the PC, [zf_optype] filled
; out: [zf_ops] filled, PC advanced; preserves all registers
;
; First to last, and that order is load-bearing: Standard 4.5.2 says the stack
; pointer may appear twice in one instruction and @sub sp sp means "top minus
; second-from-top", which only comes out right if the pops happen in order.
; -----------------------------------------------------------------------------
zx_getops:
    push ax
    push bx
    push cx
    push di
    mov cl, [zf_opn]
    xor ch, ch
    jcxz .done
    xor di, di
.l:
    mov bl, [zf_optype+di]
    or bl, bl
    jnz .small
    mov ah, [es:si]                 ; large constant: big-endian (4.2.1)
    mov al, [es:si+1]
    add si, 2
    jmp short .put
.small:
    cmp bl, 1
    jne .var
    mov al, [es:si]
    inc si
    xor ah, ah
    jmp short .put
.var:
    mov al, [es:si]
    inc si
    call zx_getvar                  ; "variable by value": variable 0 POPS
.put:
    mov bx, di
    shl bx, 1
    mov [zf_ops+bx], ax
    inc di
    loop .l
.done:
    pop di
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; Variables (Standard 4.2.2, 6.2, 6.3)
;
; Four kinds behind one byte: 0 is the stack, 1-15 are the current routine's
; locals, 16-255 are the globals. The distinction the Standard makes twice and
; every buggy interpreter misses once is DIRECT versus INDIRECT (6.3.4): the
; seven opcodes that name a variable by reference - @inc, @dec, @inc_chk,
; @dec_chk, @load, @store, @pull - read and write the TOP OF STACK IN PLACE,
; while an ordinary operand fetch or result store pops and pushes. That is why
; there are two readers and two writers here and not one of each.
; =============================================================================

; -----------------------------------------------------------------------------
; zx_getvar - read a variable, DIRECTLY (variable 0 pops)
; in:  AL = variable number.  out: AX = its value; all else preserved
; -----------------------------------------------------------------------------
zx_getvar:
    or al, al
    jz zx_pop
    cmp al, 0x10
    jae .glob
    push bx
    push es
    cmp al, [zf_nlocals]            ; a local this routine does not have is
    ja .nolocal                     ; illegal (4.2.2); answer 0, do not fault
    mov bl, al
    xor bh, bh
    dec bx
    shl bx, 1
    add bx, [zf_fp]
    add bx, ZXF_LOC
    mov es, [zf_stkseg]
    mov ax, [es:bx]
    pop es
    pop bx
    ret
.nolocal:
    xor ax, ax
    pop es
    pop bx
    ret
.glob:
    push bx
    mov bl, al
    xor bh, bh
    sub bx, 0x10
    shl bx, 1
    add bx, [zf_gvar]
    mov ax, bx
    pop bx
    call zm_gw16
    ret

; -----------------------------------------------------------------------------
; zx_getvar_i - read a variable, INDIRECTLY (variable 0 reads in place)
; in:  AL = variable number.  out: AX; all else preserved
; -----------------------------------------------------------------------------
zx_getvar_i:
    or al, al
    jnz zx_getvar
    push bx
    push cx
    push es
    call zx_ebase
    mov cx, ax
    mov bx, [zf_sp]
    cmp bx, cx
    jbe .none
    sub bx, 2
    mov es, [zf_stkseg]
    mov ax, [es:bx]
    jmp short .out
.none:
    xor ax, ax
.out:
    pop es
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zx_store - write a variable, DIRECTLY (variable 0 pushes)
; in:  AL = variable number, BX = value.  out: nothing; all preserved
; -----------------------------------------------------------------------------
zx_store:
    or al, al
    jnz zx_setvar_body
    push ax
    mov ax, bx
    call zx_push
    pop ax
    ret

; -----------------------------------------------------------------------------
; zx_setvar - write a variable, INDIRECTLY (variable 0 overwrites the top)
; in:  AL = variable number, BX = value.  out: nothing; all preserved
; -----------------------------------------------------------------------------
zx_setvar:
    or al, al
    jnz zx_setvar_body
    push ax
    push di
    push cx
    push es
    call zx_ebase
    mov cx, ax
    mov di, [zf_sp]
    cmp di, cx
    jbe .none                       ; nothing on the stack: nothing to replace
    sub di, 2
    mov es, [zf_stkseg]
    mov [es:di], bx
.none:
    pop es
    pop cx
    pop di
    pop ax
    ret

zx_setvar_body:
    push ax
    push di
    push es
    xor ah, ah
    cmp al, 0x10
    jae .glob
    cmp al, [zf_nlocals]
    ja .out
    dec ax
    shl ax, 1
    mov di, ax
    add di, [zf_fp]
    add di, ZXF_LOC
    mov es, [zf_stkseg]
    mov [es:di], bx
.out:
    pop es
    pop di
    pop ax
    ret
.glob:
    sub ax, 0x10
    shl ax, 1
    add ax, [zf_gvar]
    call zm_pw16
    jmp short .out

; -----------------------------------------------------------------------------
; zx_ebase - where the current frame's evaluation stack starts
; in:  nothing.  out: AX = a byte offset into [zf_stkseg]; all else preserved
; -----------------------------------------------------------------------------
zx_ebase:
    mov al, [zf_nlocals]
    xor ah, ah
    shl ax, 1
    add ax, ZXF_LOC
    add ax, [zf_fp]
    ret

; -----------------------------------------------------------------------------
; zx_push / zx_pop - the evaluation stack
; in:  AX = value / nothing.  out: nothing / AX = value; all else preserved
;
; Both bounds are checked, and both are checked against the FRAME, not against
; zero: Standard 6.3.1 says the stack is empty at routine entry, so a pull
; that reaches below this frame's base is the story's bug and not a licence to
; read the caller's locals.
; -----------------------------------------------------------------------------
zx_push:
    push bx
    push es
    mov bx, [zf_sp]
    cmp bx, ZX_STKBYTES - 2
    jae .over
    mov es, [zf_stkseg]
    mov [es:bx], ax
    add bx, 2
    mov [zf_sp], bx
.out:
    pop es
    pop bx
    ret
.over:
    push ax
    mov al, ZXE_OSTACK
    call zx_halt
    pop ax
    jmp short .out

zx_pop:
    push bx
    push cx
    push es
    call zx_ebase
    mov cx, ax
    mov bx, [zf_sp]
    cmp bx, cx
    jbe .under
    sub bx, 2
    mov [zf_sp], bx
    mov es, [zf_stkseg]
    mov ax, [es:bx]
.out:
    pop es
    pop cx
    pop bx
    ret
.under:
    mov al, ZXE_USTACK
    call zx_halt
    xor ax, ax
    jmp short .out

; -----------------------------------------------------------------------------
; zx_storeres - fetch this instruction's store byte and put a result in it
; in:  BX = the value; ES:SI = the PC, on the store byte (Standard 4.6)
; out: PC advanced one byte; preserves all registers
; -----------------------------------------------------------------------------
zx_storeres:
    push ax
    mov al, [es:si]
    inc si
    mov [zf_store], al
    call zx_store
    pop ax
    ret

; =============================================================================
; Branches (Standard 4.7)
; =============================================================================

; -----------------------------------------------------------------------------
; zx_branch - read the branch data at the PC and act on a condition
; in:  CF = the condition (set = the test was TRUE); ES:SI = the PC, on the
;      first branch byte
; out: the PC moved or advanced; preserves all registers
;
; Bit 7 is the polarity, bit 6 picks the one-byte (0..63 unsigned) form from
; the two-byte (signed 14-bit) one, and offsets 0 and 1 are not offsets at all
; but "return false" and "return true" (4.7.1) - which is why this routine has
; to be able to unwind a frame and not merely add to SI.
; -----------------------------------------------------------------------------
zx_branch:
    push ax
    push bx
    push cx
    push dx
    mov dl, 0
    jnc .nc
    mov dl, 1
.nc:
    mov al, [es:si]
    inc si
    mov cl, 0
    test al, 0x80
    jz .p0
    mov cl, 1
.p0:
    mov bl, al
    and bl, 0x3F
    xor bh, bh
    test al, 0x40
    jnz .have
    mov bh, bl                      ; two-byte form: 14 bits, sign in bit 13
    mov bl, [es:si]
    inc si
    test bh, 0x20
    jz .have
    or bh, 0xC0
.have:
    cmp dl, cl
    jne .no                         ; condition disagrees with polarity
    or bx, bx
    jz .rfalse
    cmp bx, 1
    je .rtrue
    sub bx, 2                       ; Standard 4.7.2
    call zx_jrel
.no:
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.rfalse:
    xor ax, ax
    call zx_ret
    jmp short .no
.rtrue:
    mov ax, 1
    call zx_ret
    jmp short .no

; Two shorthands: almost every branch opcode ends in one of these.
zx_bt:
    stc
    call zx_branch
    ret
zx_bf:
    clc
    call zx_branch
    ret

; -----------------------------------------------------------------------------
; zx_jrel - move the PC by a signed byte count
; in:  BX = the signed delta, ES:SI = the PC
; out: ES:SI moved; preserves all registers
;
; SI is only ever in 0..0x8015 at this point (zx_step renormalises at 0x8000
; and no instruction is longer than 21 bytes), and a branch offset is at most
; +/-8191, so exactly one case needs a segment adjustment: a backwards move
; that goes below zero. `add si, bx` with BX negative clears CF precisely when
; that happened, and 0x1000 paragraphs is the 65536 bytes SI wrapped by.
;
; THE WHOLE OFFSET IS THEN FOLDED BACK INTO THE SEGMENT, and that is not
; tidiness. `sub es, 0x1000` alone leaves ES:SI addressing the right BYTE with
; SI up near 0xFFFF and ES a full 64KB below where the story starts - a legal
; alias of a legal address that nothing downstream accepts. zx_step's guard
; compares ES against [zf_sseg] and calls it a lost PC; zm_pcaddr subtracts
; [zf_sseg] and reports an address a megabyte out, so the return address a
; @call banks is wrong and the matching @ret fails zm_inimg. It bit whenever
; the PC was in the story's first 64KB and a backwards branch reached further
; back than SI had walked - which is every loop whose body contains a call,
; because @ret comes through zm_seek and zm_seek leaves SI in 0..15. That is
; the halt docs/plans/completed/FROTZ-PLAN.md 10.1 describes: the guard was right that
; something had gone wrong and wrong about what, because the PC in front of it
; was correct and merely unnormalised.
; -----------------------------------------------------------------------------
zx_jrel:
    or bx, bx
    js .neg
    add si, bx
    ret
.neg:
    add si, bx
    jc .ok                          ; no borrow: SI stayed above zero
    push ax
    push cx
    mov ax, si                      ; SI is now the true offset MINUS 0x10000,
    mov cl, 4                       ; so its top 12 bits are paragraphs the
    shr ax, cl                      ; segment is owed
    and si, 15
    mov cx, es
    sub cx, 0x1000                  ; ...less the 64KB SI wrapped by
    add ax, cx
    mov es, ax
    mov [zf_pcseg], ax              ; a PC mover; see zm_seek in zmem.inc
    mov [zf_pcoff], si
    pop cx
    pop ax
.ok:
    ret

; =============================================================================
; Routine calls and returns (Standard 6.4)
; =============================================================================

; -----------------------------------------------------------------------------
; zx_call - push a frame and jump to a routine
; in:  DX:AX = the routine's BYTE address (already unpacked), CX = argument
;      count, the arguments in [zf_ops+2] onwards (operand 0 is the routine
;      itself in every call opcode, so the args sit there naturally),
;      BL = the store variable, 0xFF to discard the result
; out: ES:SI = the routine's first instruction; preserves all registers
;
; Standard 6.4.4: v1-4 take each local's initial value from the routine
; header, v5+ zero them, and then the arguments overwrite locals 1..n. 6.4.4.1
; makes both mismatches legal, so spare arguments are dropped and missing ones
; keep their initial value.
; -----------------------------------------------------------------------------
zx_call:
    push ax
    push bx
    push cx
    push dx
    push di
    push es

    mov di, ax
    or di, dx
    jnz .real
    ; Standard 6.4.3: calling packed address 0 does nothing and returns false
    cmp bl, 0xFF
    je .out
    mov al, bl
    xor bx, bx
    call zx_store
.out:
    pop es
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

.real:
    mov [zx_ra_l], ax
    mov [zx_ra_h], dx
    mov [zx_tmp0], cx               ; argument count
    xor bh, bh
    mov [zx_tmp1], bx               ; store variable, in the low byte

    call zm_pcaddr                  ; DX:AX = where to come back to
    mov [zx_rp_l], ax
    mov [zx_rp_h], dx

    mov ax, [zx_ra_l]
    mov dx, [zx_ra_h]
    call zm_gb                      ; AL = the local count
    xor ah, ah
    cmp ax, 15
    jbe .nok
    mov al, ZXE_LOCALS
    call zx_halt
    jmp short .out
.nok:
    mov [zx_tmp2], ax               ; n

    mov di, [zf_sp]
    mov [zx_tmp3], di               ; the new frame's base
    mov ax, [zx_tmp2]
    shl ax, 1
    add ax, ZXF_LOC
    add ax, di
    cmp ax, ZX_STKBYTES
    jb .room
    mov al, ZXE_OSTACK
    call zx_halt
    jmp short .out
.room:
    mov es, [zf_stkseg]
    mov ax, [zf_fp]
    mov [es:di+ZXF_PREV], ax
    mov ax, [zx_rp_l]
    mov [es:di+ZXF_PCL], ax
    mov ax, [zx_rp_h]
    mov [es:di+ZXF_PCH], ax
    mov ax, [zx_tmp1]
    mov [es:di+ZXF_STORE], al
    mov ax, [zx_tmp2]
    mov [es:di+ZXF_NLOC], al
    mov ax, [zx_tmp0]
    mov [es:di+ZXF_ARGC], ax

    ; --- the locals, in their initial state -------------------------------
    mov cx, [zx_tmp2]
    jcxz .args
    add di, ZXF_LOC
    cmp byte [zf_ver], 5
    jae .zeros
    xor bx, bx                      ; v1-4: the 2n bytes after the header byte
.initl:
    mov ax, [zx_ra_l]
    mov dx, [zx_ra_h]
    add ax, bx
    adc dx, 0
    add ax, 1
    adc dx, 0
    call zm_gw                      ; preserves ES, so the frame stays reachable
    mov [es:di], ax
    add di, 2
    add bx, 2
    loop .initl
    jmp short .args
.zeros:
    xor ax, ax
    cld                             ; the one string op in this file; ES:DI is
    rep stosw                       ; the frame, so DF is not left to chance

.args:
    ; --- the arguments over the top of them (Standard 6.4.4) ---------------
    mov cx, [zx_tmp0]
    cmp cx, [zx_tmp2]
    jbe .nclip
    mov cx, [zx_tmp2]               ; spare arguments are thrown away
.nclip:
    jcxz .setpc
    mov di, [zx_tmp3]
    add di, ZXF_LOC
    mov bx, zf_ops + 2
.copya:
    mov ax, [bx]
    mov [es:di], ax
    add bx, 2
    add di, 2
    loop .copya

.setpc:
    mov di, [zx_tmp3]
    mov [zf_fp], di
    mov ax, [zx_tmp2]
    shl ax, 1
    add ax, ZXF_LOC
    add ax, di
    mov [zf_sp], ax
    mov ax, [zx_tmp2]
    mov [zf_nlocals], al

    mov cx, 1                       ; past the header byte...
    cmp byte [zf_ver], 5
    jae .pc
    mov cx, [zx_tmp2]               ; ...and past the initial values in v1-4
    shl cx, 1
    inc cx
.pc:
    mov ax, [zx_ra_l]
    mov dx, [zx_ra_h]
    add ax, cx
    adc dx, 0
    call zm_seek                    ; ES:SI = the routine body - THE NEW PC

    ; ...and it must survive the epilogue. The `.out` path below pops the ES
    ; this proc saved on entry, which is the CALLER's segment - restoring it
    ; here moves only the offset, and a routine whose body happens to land on
    ; the same paragraph offset as the call that reached it then re-executes
    ; the call for ever. That is not hypothetical: it is exactly what
    ; ZOPSTEST.Z3 did - call at 0x0533 (offset 3), routine body at 0x0993
    ; (offset 3) - and the story halted on a stack overflow having printed
    ; nothing. So this path drops the saved ES instead of restoring it.
    add sp, 2                       ; discard the stacked ES; SI is already ours
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; -----------------------------------------------------------------------------
; zx_ret - pop the current frame and return a value
; in:  AX = the return value
; out: ES:SI = the caller's PC; preserves every register but ES:SI
;
; Standard 6.3.2 makes this cheap: everything the routine pushed is discarded
; by setting the stack pointer back to the frame base, so there is no unwind
; loop and no way for a routine to leak stack into its caller.
; -----------------------------------------------------------------------------
; zx_inimage - is DX:AX inside the story image?
; out: CF=0 yes; preserves every register
zx_inimage:
    cmp dx, [zf_slen_h]
    jb .ok
    ja .no
    cmp ax, [zf_slen_l]
    jb .ok
.no:
    stc
    ret
.ok:
    clc
    ret

zx_ret:
    push ax
    push bx
    push cx
    push dx
    push di
    mov [zx_tmp0], ax               ; the return value, past the frame swap
    mov di, [zf_fp]
    push es
    mov es, [zf_stkseg]
    mov bx, [es:di+ZXF_PREV]
    inc bx
    jz .bottom                      ; 0xFFFF: this is the dummy frame
    dec bx
    mov cx, [es:di+ZXF_PCL]
    mov [zx_tmp1], cx
    mov cx, [es:di+ZXF_PCH]
    mov [zx_tmp2], cx
    mov cl, [es:di+ZXF_STORE]
    xor ch, ch
    mov [zx_tmp3], cx
    mov cl, [es:bx+ZXF_NLOC]
    mov [zf_nlocals], cl
    mov [zf_sp], di
    mov [zf_fp], bx
    pop es

    mov ax, [zx_tmp1]
    mov dx, [zx_tmp2]
    mov cx, ax
    or cx, dx
    jz .fell                        ; v6's main routine returned: that is @quit
    ; The frame's saved PC is checked before it becomes the PC. A frame that
    ; has been overwritten sends the interpreter to an address with no story
    ; at it, and the halt that follows names an innocent opcode several
    ; instructions later; caught HERE it names the return itself.
    call zx_inimage
    jc .badret
    call zm_seek
    mov al, [zx_tmp3]
    cmp al, 0xFF
    je .done
    mov bx, [zx_tmp0]
    call zx_store
    jmp short .done
.badret:
    mov [zx_badpc_l], ax            ; the address the frame asked to return to
    mov [zx_badpc_h], dx
    mov byte [zx_badop], 0xFE       ; 0xFE = "a return, not an opcode"
    mov al, ZXE_BADOP
    call zx_halt
.done:
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret
.bottom:
    pop es
    mov al, ZXE_RET
    call zx_halt
    jmp short .done
.fell:
    mov byte [zf_dead], 1
    jmp short .done

; -----------------------------------------------------------------------------
; zx_halt - stop the story, with a reason (SPEC.md 47)
; in:  AL = a ZXE_* code
; out: nothing; preserves all registers. The worker prints it and parks.
; -----------------------------------------------------------------------------
zx_halt:
    push bx
    mov [zf_err], al
    mov bl, al
    sub bl, ZXE_BASE
    cmp bl, ZXE_COUNT
    jb .known
    xor bl, bl
.known:
    xor bh, bh
    shl bx, 1
    mov bx, [zx_msgs + bx]
    mov [zx_msg], bx
    pop bx
    ret

; The decoder's two "no such opcode" arms land here. They are jumped to from
; zx_step, so the `ret` returns to zx_run, which sees [zf_err] and stops.
; The PC left the story image, and the instruction whose handler is at fault
; is the one that JUST RAN - which is [zx_op], still holding the opcode this
; guard is standing in front of. It used to report a separate [zx_prevop], and
; that was one instruction too far back: the guard runs BEFORE the decode that
; would have rolled the two over, so the name in the halt was never the name
; of the culprit - it named the instruction before it, every time.
zx_lost:
    mov al, [zx_op]
    mov [zx_badop], al
    mov ax, es                      ; the segment we ended up in, which names
    mov [zx_badseg], ax             ; the claim whose owner clobbered ES
    call zm_pcaddr
    mov [zx_badpc_l], ax
    mov [zx_badpc_h], dx
    mov al, ZXE_BADOP
    call zx_halt
    ret

zx_bad:
    push ax
    push dx
    mov al, [zx_op]
    mov [zx_badop], al
    call zm_pcaddr                  ; DX:AX = where we are, past the opcode
    mov [zx_badpc_l], ax
    mov [zx_badpc_h], dx
    pop dx
    pop ax
    mov al, ZXE_BADOP
    call zx_halt
    ret


; -----------------------------------------------------------------------------
; zx_report - put the halt reason on screen
; in:  [zf_err] set; the gfx lock NOT held
; out: nothing; preserves all registers
; -----------------------------------------------------------------------------
zx_report:
    push ax
    push si
    call OSAPI_GFX_LOCK
    mov al, [zf_err]
    sub al, ZXE_BASE
    cmp al, ZXE_COUNT
    jb .ours
    mov si, zx_m_other
    jmp short .say
.ours:
    mov si, [zx_msg]
    or si, si
    jnz .say
    mov si, zx_m_other
.say:
    push es
    call zw_notice
    pop es
    ; An unknown opcode says WHICH and WHERE. Without that the reader is sent
    ; to a disassembler to find out what stopped, which defeats the point of
    ; stopping cleanly rather than executing wild (SPEC.md 47).
    cmp byte [zf_err], ZXE_BADOP
    jne .nodetail
    mov si, zx_m_opat
    push es
    call zt_puts
    pop es
    mov al, [zx_badop]
    xor ah, ah
    call zx_hex
    mov si, zx_m_raw                ; the opcode BYTE too: 0x05 alone is five
    push es                         ; different instructions and the form is
    call zt_puts                    ; the only thing that separates them
    pop es
    mov al, [zx_raw]
    xor ah, ah
    call zx_hex
    mov si, zx_m_at
    push es
    call zt_puts
    pop es
    mov ax, [zx_badpc_h]
    call zx_hex
    mov ax, [zx_badpc_l]
    call zx_hex
    mov si, zx_m_seg
    push es
    call zt_puts
    pop es
    mov ax, [zx_badseg]
    call zx_hex
    mov si, zx_m_base
    push es
    call zt_puts
    pop es
    mov ax, [zf_sseg]
    call zx_hex
    mov al, 13
    push es
    call zt_putc
    pop es
.nodetail:
    call OSAPI_GFX_UNLOCK
    pop si
    pop ax
    ret

zx_msgs:
    dw zx_m_badop, zx_m_div0, zx_m_ustack, zx_m_ostack
    dw zx_m_locals, zx_m_ret, zx_m_frame
zx_m_badop:  db '[Frotz: unknown opcode - the story has stopped.]', 0
zx_m_div0:   db '[Frotz: division by zero - the story has stopped.]', 0
zx_m_ustack: db '[Frotz: stack underflow - the story has stopped.]', 0
zx_m_ostack: db '[Frotz: stack overflow - the story has stopped.]', 0
zx_m_opat:   db '  opcode 0x', 0
zx_m_raw:    db ' byte 0x', 0
zx_m_at:     db ' at 0x', 0
zx_m_seg:    db ' es=', 0
zx_m_base:   db ' story=', 0
zx_m_locals: db '[Frotz: bad routine header - the story has stopped.]', 0
zx_m_ret:    db '[Frotz: return with no caller - the story has stopped.]', 0
zx_m_frame:  db '[Frotz: bad stack frame - the story has stopped.]', 0
zx_m_other:  db '[Frotz: the story has stopped.]', 0

; =============================================================================
; Arithmetic helpers
; =============================================================================

; -----------------------------------------------------------------------------
; zx_sdiv - signed 16-bit division that cannot fault an 8086
; in:  AX = dividend, BX = divisor
; out: AX = quotient, DX = remainder; preserves all else
;
; Two inputs turn `idiv` into an int 0, and this kernel installs no handler for
; it: a zero divisor, and -32768 / -1 whose quotient will not fit AX. Both are
; tested before the instruction rather than trapped after it. The Standard
; calls division by zero illegal, so it halts with a message; the overflow is
; a legal calculation and answers -32768, which is what the wrap gives.
; -----------------------------------------------------------------------------
zx_sdiv:
    or bx, bx
    jz .zero
    cmp bx, -1
    jne .ok
    neg ax
    xor dx, dx
    ret
.ok:
    cwd
    idiv bx
    ret
.zero:
    push ax
    mov al, ZXE_DIV0
    call zx_halt
    pop ax
    xor ax, ax
    xor dx, dx
    ret

; -----------------------------------------------------------------------------
; zx_rand16 - the generator behind @random (Standard 2.4)
; in:  nothing.  out: AX = the next word; preserves all else
;
; A 16-bit xorshift (7, 9, 8) with a stirring counter. It is not the ANSI C
; rand() the Standard's remarks warn about, it has no short cycles from small
; seeds - 2.4.2 asks for exactly that - and it costs three shifts rather than
; a 32-bit multiply the 8086 would have to synthesise.
; -----------------------------------------------------------------------------
zx_rand16:
    push bx
    push cx
    mov ax, [zf_rndlo]
    or ax, ax
    jnz .nz
    mov ax, 2463                    ; the state 0 is the one fixed point
.nz:
    mov bx, ax
    mov cl, 7
    shl bx, cl
    xor ax, bx
    mov bx, ax
    mov cl, 9
    shr bx, cl
    xor ax, bx
    mov bx, ax
    mov cl, 8
    shl bx, cl
    xor ax, bx
    mov [zf_rndlo], ax
    inc word [zf_rndhi]
    add ax, [zf_rndhi]
    pop cx
    pop bx
    ret

; -----------------------------------------------------------------------------
; zx_v6 - is this a Version 6 story?
; in:  nothing.  out: ZF = 1 if v6; preserves all registers
; -----------------------------------------------------------------------------
zx_v6:
    cmp byte [zf_ver], 6
    ret

; =============================================================================
; 2OP opcodes (Standard 15)
; Each is entered by a jump from zx_step and leaves by `ret`, so the operands
; are in [zf_ops], the count in [zf_opn], and ES:SI is the store/branch data.
; =============================================================================

zxo_je:                             ; 2OP:1  je a b ?(label)
    ; In variable form @je takes up to four operands and is true if the first
    ; equals ANY of the rest - the one 2OP that is not a two-operand opcode.
    mov ax, [zf_ops]
    mov cl, [zf_opn]
    xor ch, ch
    or cx, cx
    jz zx_bf
    dec cx
    jz zx_bf
    mov di, zf_ops + 2
.l:
    cmp ax, [di]
    je zx_bt
    add di, 2
    loop .l
    jmp zx_bf

zxo_jl:                             ; 2OP:2  jl a b ?(label) - signed
    mov ax, [zf_ops]
    cmp ax, [zf_ops+2]
    jl zx_bt
    jmp zx_bf

zxo_jg:                             ; 2OP:3  jg a b ?(label) - signed
    mov ax, [zf_ops]
    cmp ax, [zf_ops+2]
    jg zx_bt
    jmp zx_bf

zxo_dec_chk:                        ; 2OP:4  dec_chk (variable) value ?(label)
    mov al, byte [zf_ops]
    push ax
    call zx_getvar_i                ; indirect: Standard 6.3.4
    dec ax
    mov bx, ax
    pop ax
    push bx
    call zx_setvar
    pop ax
    cmp ax, [zf_ops+2]
    jl zx_bt
    jmp zx_bf

zxo_inc_chk:                        ; 2OP:5  inc_chk (variable) value ?(label)
    mov al, byte [zf_ops]
    push ax
    call zx_getvar_i
    inc ax
    mov bx, ax
    pop ax
    push bx
    call zx_setvar
    pop ax
    cmp ax, [zf_ops+2]
    jg zx_bt
    jmp zx_bf

zxo_jin:                            ; 2OP:6  jin obj1 obj2 ?(label)
    mov ax, [zf_ops]
    call zo_parent
    cmp ax, [zf_ops+2]
    je zx_bt
    jmp zx_bf

zxo_test:                           ; 2OP:7  test bitmap flags ?(label)
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    and ax, bx
    cmp ax, bx
    je zx_bt
    jmp zx_bf

zxo_or:                             ; 2OP:8  or a b -> (result)
    mov bx, [zf_ops]
    or bx, [zf_ops+2]
    call zx_storeres
    ret

zxo_and:                            ; 2OP:9  and a b -> (result)
    mov bx, [zf_ops]
    and bx, [zf_ops+2]
    call zx_storeres
    ret

zxo_test_attr:                      ; 2OP:10 test_attr object attribute ?(label)
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    call zo_attr                    ; out CF = the bit, which IS the condition
    call zx_branch
    ret

zxo_set_attr:                       ; 2OP:11 set_attr object attribute
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    call zo_attr_set
    ret

zxo_clear_attr:                     ; 2OP:12 clear_attr object attribute
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    call zo_attr_clr
    ret

zxo_store:                          ; 2OP:13 store (variable) value
    mov al, byte [zf_ops]
    mov bx, [zf_ops+2]
    call zx_setvar                  ; indirect (Standard 6.3.4)
    ret

zxo_insert_obj:                     ; 2OP:14 insert_obj object destination
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    call zo_insert
    ret

zxo_loadw:                          ; 2OP:15 loadw array word-index -> (result)
    ; The sum wraps at 16 bits, which is what every interpreter does and what
    ; stories that index near 0xFFFF rely on; dynamic and static memory are
    ; below 64KB by construction, so zm_gw16 is the whole of the reach needed.
    mov ax, [zf_ops+2]
    shl ax, 1
    add ax, [zf_ops]
    call zm_gw16
    mov bx, ax
    call zx_storeres
    ret

zxo_loadb:                          ; 2OP:16 loadb array byte-index -> (result)
    mov ax, [zf_ops+2]
    add ax, [zf_ops]
    call zm_gb16
    xor ah, ah
    mov bx, ax
    call zx_storeres
    ret

zxo_get_prop:                       ; 2OP:17 get_prop object property -> (result)
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    call zo_getprop
    mov bx, ax
    call zx_storeres
    ret

zxo_get_prop_addr:                  ; 2OP:18 get_prop_addr obj prop -> (result)
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    call zo_propaddr
    mov bx, ax
    call zx_storeres
    ret

zxo_get_next_prop:                  ; 2OP:19 get_next_prop obj prop -> (result)
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    call zo_nextprop                ; out AL
    xor ah, ah
    mov bx, ax
    call zx_storeres
    ret

zxo_add:                            ; 2OP:20 add a b -> (result)
    mov bx, [zf_ops]
    add bx, [zf_ops+2]
    call zx_storeres
    ret

zxo_sub:                            ; 2OP:21 sub a b -> (result)
    mov bx, [zf_ops]
    sub bx, [zf_ops+2]
    call zx_storeres
    ret

zxo_mul:                            ; 2OP:22 mul a b -> (result)
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    imul bx                         ; DX:AX; the Standard keeps the low word
    mov bx, ax
    call zx_storeres
    ret

zxo_div:                            ; 2OP:23 div a b -> (result) - signed
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    call zx_sdiv
    mov bx, ax
    call zx_storeres
    ret

zxo_mod:                            ; 2OP:24 mod a b -> (result) - signed
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    call zx_sdiv
    mov bx, dx
    call zx_storeres
    ret

zxo_call_2s:                        ; 2OP:25 call_2s routine arg1 -> (result)
    mov al, [es:si]                 ; the store byte comes BEFORE the jump
    inc si
    mov [zf_store], al
    mov bl, al
    push bx
    mov ax, [zf_ops]
    mov bl, 0
    call zm_unpack
    pop bx
    mov cx, 1
    call zx_call
    ret

zxo_call_2n:                        ; 2OP:26 call_2n routine arg1
    mov ax, [zf_ops]
    mov bl, 0
    call zm_unpack
    mov cx, 1
    mov bl, 0xFF                    ; the result is discarded
    call zx_call
    ret

zxo_set_colour:                     ; 2OP:27 set_colour foreground background
    ; Honoured on VGA, ignored on the 1bpp adapters by zwin (FROTZ-PLAN 5.1):
    ; every colour rounds to black or white there, and a story that colours by
    ; meaning would become unreadable rather than plain.
    mov al, byte [zf_ops]
    mov ah, byte [zf_ops+2]
    push es
    call zw_colour
    pop es
    ret

zxo_throw:                          ; 2OP:28 throw value stack-frame
    ; Standard 6.5: the operand is a frame index @catch produced, so it is
    ; validated against the live chain rather than trusted - an index off the
    ; chain would otherwise set [zf_fp] to an arbitrary offset in the claim.
    push es
    mov es, [zf_stkseg]
    mov bx, [zf_fp]
    mov cx, [zf_ops+2]
.walk:
    cmp bx, cx
    je .found
    mov ax, [es:bx+ZXF_PREV]
    inc ax
    jz .badframe
    dec ax
    mov bx, ax
    jmp short .walk
.found:
    mov [zf_fp], bx
    mov al, [es:bx+ZXF_NLOC]
    mov [zf_nlocals], al
    pop es
    mov ax, [zf_ops]
    call zx_ret
    ret
.badframe:
    pop es
    mov al, ZXE_FRAME
    call zx_halt
    ret

; =============================================================================
; 1OP opcodes
; =============================================================================

zxo_jz:                             ; 1OP:128 jz a ?(label)
    cmp word [zf_ops], 0
    je zx_bt
    jmp zx_bf

zxo_get_sibling:                    ; 1OP:129 get_sibling obj -> (result) ?(label)
    mov ax, [zf_ops]
    call zo_sibling
    mov bx, ax
    push bx
    call zx_storeres                ; the store byte precedes the branch data
    pop bx
    or bx, bx
    jnz zx_bt
    jmp zx_bf

zxo_get_child:                      ; 1OP:130 get_child obj -> (result) ?(label)
    mov ax, [zf_ops]
    call zo_child
    mov bx, ax
    push bx
    call zx_storeres
    pop bx
    or bx, bx
    jnz zx_bt
    jmp zx_bf

zxo_get_parent:                     ; 1OP:131 get_parent object -> (result)
    mov ax, [zf_ops]
    call zo_parent
    mov bx, ax
    call zx_storeres
    ret

zxo_get_prop_len:                   ; 1OP:132 get_prop_len prop-addr -> (result)
    mov ax, [zf_ops]
    or ax, ax
    jnz .real
    xor bx, bx                      ; Standard 15: get_prop_len 0 must give 0
    call zx_storeres
    ret
.real:
    xor dx, dx
    call zo_proplen
    mov bx, ax
    call zx_storeres
    ret

zxo_inc:                            ; 1OP:133 inc (variable)
    mov al, byte [zf_ops]
    push ax
    call zx_getvar_i
    inc ax
    mov bx, ax
    pop ax
    call zx_setvar
    ret

zxo_dec:                            ; 1OP:134 dec (variable)
    mov al, byte [zf_ops]
    push ax
    call zx_getvar_i
    dec ax
    mov bx, ax
    pop ax
    call zx_setvar
    ret

zxo_print_addr:                     ; 1OP:135 print_addr byte-address-of-string
    mov ax, [zf_ops]
    xor dx, dx
    push es
    call zt_print
    pop es
    ret

zxo_call_1s:                        ; 1OP:136 call_1s routine -> (result)
    mov al, [es:si]
    inc si
    mov [zf_store], al
    mov bl, al
    push bx
    mov ax, [zf_ops]
    mov bl, 0
    call zm_unpack
    pop bx
    xor cx, cx
    call zx_call
    ret

zxo_remove_obj:                     ; 1OP:137 remove_obj object
    mov ax, [zf_ops]
    call zo_remove
    ret

zxo_print_obj:                      ; 1OP:138 print_obj object
    mov ax, [zf_ops]
    call zo_name                    ; out DX:AX = the short-name Z-string
    push es
    call zt_print
    pop es
    ret

zxo_ret:                            ; 1OP:139 ret value
    mov ax, [zf_ops]
    call zx_ret
    ret

zxo_jump:                           ; 1OP:140 jump ?(label)
    ; Not a branch opcode: the operand IS the offset, and it is signed
    ; (Standard 4.7.2's arithmetic applies here too).
    mov bx, [zf_ops]
    sub bx, 2
    call zx_jrel
    ret

zxo_print_paddr:                    ; 1OP:141 print_paddr packed-addr-of-string
    mov ax, [zf_ops]
    mov bl, 1                       ; 1 = string, which is a different shift
    call zm_unpack                  ; from a routine in v6/v7
    push es
    call zt_print
    pop es
    ret

zxo_load:                           ; 1OP:142 load (variable) -> (result)
    mov al, byte [zf_ops]
    call zx_getvar_i                ; indirect (Standard 6.3.4)
    mov bx, ax
    call zx_storeres
    ret

zxo_not1:                           ; 1OP:143 - two opcodes at one number
    ; @not until v4; from v5 the slot is @call_1n and @not moved to VAR:248.
    cmp byte [zf_ver], 5
    jae .call1n
    mov bx, [zf_ops]
    not bx
    call zx_storeres
    ret
.call1n:
    mov ax, [zf_ops]
    mov bl, 0
    call zm_unpack
    xor cx, cx
    mov bl, 0xFF
    call zx_call
    ret

; =============================================================================
; 0OP opcodes
; =============================================================================

zxo_rtrue:                          ; 0OP:176
    mov ax, 1
    call zx_ret
    ret

zxo_rfalse:                         ; 0OP:177
    xor ax, ax
    call zx_ret
    ret

zxo_print:                          ; 0OP:178 print (literal-string)
    ; Standard 4.8: the text is inline and execution continues after the word
    ; with the top bit set, so ztext hands back where that was and the PC is
    ; re-seeked rather than guessed at.
    call zm_pcaddr
    push es
    call zt_print_ret ; out DX:AX = the address after the string
    pop es
    call zm_seek
    ret

zxo_print_ret:                      ; 0OP:179 print_ret (literal-string)
    call zm_pcaddr
    push es
    call zt_print_ret
    pop es
    mov al, 13
    push es
    call zt_putc
    pop es
    mov ax, 1
    call zx_ret
    ret

zxo_nop:                            ; 0OP:180
    ret

zxo_save0:                          ; 0OP:181 save - three different opcodes
    ; v1-3 branch on success, v4 stores 0/1, and from v5 the slot is illegal
    ; because @save moved to EXT:0 with its own operands. The answer is banked
    ; in DL before anything else touches the flags: a `cmp` for the version
    ; would otherwise eat the CF that IS the result.
    cmp byte [zf_ver], 5
    jae zx_bad
    call zx_spill                   ; Quetzal stores the PC of the store/branch
    call zx_unlock                  ; data, which is exactly where it stands
    call zi_save                    ; now (zio waits on the UI task, so the
    mov dl, 0                       ; lock cannot be held across it)
    jnc .ok
    mov dl, 1
.ok:
    push dx
    call zx_lock
    pop dx
    cmp byte [zf_ver], 4
    je .v4
    or dl, dl
    jz zx_bt
    jmp zx_bf
.v4:
    mov bx, 1
    or dl, dl
    jz .st
    xor bx, bx
.st:
    call zx_storeres
    ret

zxo_restore0:                       ; 0OP:182 restore - the mirror of the above
    cmp byte [zf_ver], 5
    jae zx_bad
    call zx_spill                   ; our own PC is the fallback if the file
    call zx_unlock                  ; refuses; on success zio has replaced it
    call zi_restore
    mov dl, 0
    jnc .ok
    mov dl, 1
.ok:
    push dx
    call zx_lock
    pop dx
    call zx_load                    ; ES:SI = whichever PC now stands
    cmp byte [zf_ver], 4
    je .v4
    or dl, dl
    jz zx_bt
    jmp zx_bf
.v4:
    mov bx, 2                       ; 2 = "the game has just been restored"
    or dl, dl
    jz .st
    xor bx, bx
.st:
    call zx_storeres
    ret

zxo_restart:                        ; 0OP:183 restart
    ; There is no resident pristine copy of the story (FROTZ-PLAN 3), so a
    ; restart is a re-read off the floppy and therefore UI-task work: zio owns
    ; the handshake and we only re-boot the machine once it answers.
    call zx_unlock
    call zi_restart
    pushf
    call zx_lock
    popf
    jc .no
    call zx_boot
    call zx_load
.no:
    ret

zxo_ret_popped:                     ; 0OP:184 ret_popped
    call zx_pop
    call zx_ret
    ret

zxo_pop_catch:                      ; 0OP:185 pop (v1-4) / catch (v5+)
    cmp byte [zf_ver], 5
    jae .catch
    call zx_pop                     ; the value is thrown away
    ret
.catch:
    mov bx, [zf_fp]                 ; Standard 6.5: a frame index @throw takes
    call zx_storeres
    ret

zxo_quit:                           ; 0OP:186 quit
    mov byte [zf_dead], 1
%ifdef ZHARNESS
    push si
    mov si, zh_m_quit               ; a story that ENDED, as against one that
    call zh_mark                    ; stopped: the host must tell them apart
    pop si
%endif
    ret

zxo_new_line:                       ; 0OP:187 new_line
    mov al, 13
    push es
    call zt_putc ; through the stream, so stream 3 sees it
    pop es
    ret

zxo_show_status:                    ; 0OP:188 show_status
    ; v3 only. From v4 the story owns the upper window and the opcode is
    ; illegal - but a no-op is a kinder answer than halting a story that has
    ; been mis-assembled, and it cannot corrupt anything.
    cmp byte [zf_ver], 4
    jae .no
    push es
    call zw_status
    pop es
.no:
    ret

zxo_verify:                         ; 0OP:189 verify ?(label)
    call zx_verify
    call zx_branch
    ret

zxo_piracy:                         ; 0OP:191 piracy ?(label)
    ; Standard 15: "interpreters are asked to branch on true". This copy is
    ; genuine as far as we are concerned, and there is nothing here to check.
    jmp zx_bt

; -----------------------------------------------------------------------------
; zx_verify - checksum the story against its header (Standard 11.1.1.4)
; in:  the gfx lock held.  out: CF = 1 if they agree; preserves all registers
;
; The sum runs from $40 to the LENGTH IN THE HEADER, not to the end of the
; file: some story files are padded to a page boundary and many Infocom ones
; padded with something other than zero, so including the tail fails a game
; that is perfectly intact.
;
; This is the one opcode that is measured in seconds rather than microseconds -
; half a megabyte of `mov`/`add` is ~2s on a 4.77MHz 8088 - so it drops the
; lock every 4KB, which is ~16ms of work, well inside one 55ms tick, and calls
; zi_yield there so the desktop and the clock keep running through it.
; -----------------------------------------------------------------------------
zx_verify:
    push ax
    push bx
    push cx
    push dx
    push di
    push si
    push es
    mov word [zx_sum], 0
    mov word [zx_tmp0], 0x40        ; the running address, low
    mov word [zx_tmp1], 0           ; ...and high
    mov ax, [zf_slen_l]
    mov dx, [zf_slen_h]
    sub ax, 0x40
    sbb dx, 0
    js .fail                        ; a length under $40 is not a story
    mov [zx_tmp2], ax               ; bytes remaining, low
    mov [zx_tmp3], dx               ; ...and high
.chunk:
    mov ax, [zx_tmp2]
    or ax, [zx_tmp3]
    jz .done
    mov cx, 0x1000
    cmp word [zx_tmp3], 0
    jne .full
    cmp [zx_tmp2], cx
    jae .full
    mov cx, [zx_tmp2]
.full:
    mov ax, [zx_tmp0]
    mov dx, [zx_tmp1]
    call zm_addr                    ; ES:SI = the chunk, and NOT the PC
    mov di, [zx_sum]
    xor bh, bh
.byte:
    mov bl, [es:si]
    inc si
    add di, bx
    loop .byte
    mov [zx_sum], di
    ; advance and give the machine back
    mov ax, [zx_tmp0]
    mov dx, [zx_tmp1]
    mov cx, 0x1000
    cmp word [zx_tmp3], 0
    jne .step
    cmp [zx_tmp2], cx
    jae .step
    mov cx, [zx_tmp2]
.step:
    add ax, cx
    adc dx, 0
    mov [zx_tmp0], ax
    mov [zx_tmp1], dx
    mov ax, [zx_tmp2]
    sub ax, cx
    mov [zx_tmp2], ax
    sbb word [zx_tmp3], 0
    call zx_unlock
    call zi_yield                   ; OSAPI_TASK_ALIVE + a yield, lock free
    call zx_lock
    jmp short .chunk
.done:
    mov ax, 0x1C                    ; header word 9: the file's own checksum
    call zm_gw16
    cmp ax, [zx_sum]
    jne .fail
    stc
    jmp short .out
.fail:
    clc
.out:
    pop es
    pop si
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; =============================================================================
; VAR opcodes
; =============================================================================

zxo_call:                           ; VAR:224 call / call_vs -> (result)
    mov al, [es:si]
    inc si
    mov [zf_store], al
    mov bl, al
    push bx
    mov ax, [zf_ops]
    mov bl, 0
    call zm_unpack
    pop bx
    mov cl, [zf_opn]
    xor ch, ch
    or cx, cx
    jz .noargs
    dec cx                          ; operand 0 is the routine, not an argument
.noargs:
    call zx_call
    ret

zxo_call_vn:                        ; VAR:249 call_vn / VAR:250 call_vn2
    mov ax, [zf_ops]
    mov bl, 0
    call zm_unpack
    mov cl, [zf_opn]
    xor ch, ch
    or cx, cx
    jz .noargs
    dec cx
.noargs:
    mov bl, 0xFF
    call zx_call
    ret

zxo_storew:                         ; VAR:225 storew array word-index value
    mov ax, [zf_ops+2]
    shl ax, 1
    add ax, [zf_ops]
    mov bx, [zf_ops+4]
    call zm_pw16
    ret

zxo_storeb:                         ; VAR:226 storeb array byte-index value
    mov ax, [zf_ops+2]
    add ax, [zf_ops]
    mov bl, byte [zf_ops+4]
    call zm_pb16
    ret

zxo_put_prop:                       ; VAR:227 put_prop object property value
    mov ax, [zf_ops]
    mov bl, byte [zf_ops+2]
    mov cx, [zf_ops+4]
    call zo_putprop
    ret

zxo_read:                           ; VAR:228 sread (v1-4) / aread (v5+)
    ; Standard 15 read. Everything about the two buffers - lower-casing, the
    ; leftover-characters rule, the 4-byte parse blocks - is zio's and zdict's;
    ; what belongs here is the version's idea of "how many characters fit" and
    ; the v1-3 promise that the status line is redrawn first.
    cmp byte [zf_ver], 4
    jae .nostat
    push es
    call zw_status
    pop es
.nostat:
    mov ax, [zf_ops]
    call zm_gb16                    ; byte 0 = the buffer's capacity
    xor ah, ah
    cmp byte [zf_ver], 5
    jae .max
    or ax, ax
    jz .max
    dec ax                          ; v1-4 store capacity MINUS ONE there
.max:
    mov cx, ax
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    call zx_spill
    call zx_unlock                  ; a worker that waits under the lock is a
    call zi_readline                ; hung machine (SPEC.md 20.6 rule 3)
    push ax
    call zx_lock
    pop ax
    call zx_load                    ; zio may have restored a game under us
    cmp byte [zf_ver], 5
    jb .done
    xor ah, ah
    mov bx, ax
    call zx_storeres                ; v5+: the terminating character
.done:
    ret

zxo_print_char:                     ; VAR:229 print_char output-character-code
    mov al, byte [zf_ops]
    push es
    call zt_putc
    pop es
    ret

zxo_print_num:                      ; VAR:230 print_num value
    mov ax, [zf_ops]
    push es
    call zt_num
    pop es
    ret

zxo_random:                         ; VAR:231 random range -> (result)
    ; Standard 15 + 2.4. A positive range draws a number; a negative one seeds
    ; and answers 0; zero reseeds "in as random a way as the interpreter can",
    ; which here is the tick counter. The small-negative case is the debugging
    ; sequence Enchanter's #random verb wants: seeds of 1..1000 give 1, 2, 3...
    mov ax, [zf_ops]
    or ax, ax
    jg .draw
    jz .clock
    neg ax
    cmp ax, 1000
    ja .seed
    mov [zf_rndseq], ax             ; the predictable sequence
    mov word [zf_rndctr], 0
    xor bx, bx
    call zx_storeres
    ret
.seed:
    mov [zf_rndlo], ax
    mov word [zf_rndhi], 0
    mov word [zf_rndseq], 0
    xor bx, bx
    call zx_storeres
    ret
.clock:
    call zx_seedclock
    mov word [zf_rndseq], 0
    xor bx, bx
    call zx_storeres
    ret
.draw:
    mov cx, ax                      ; CX = the range, 1..32767
    cmp word [zf_rndseq], 0
    je .real
    mov ax, [zf_rndctr]
    inc ax
    cmp ax, [zf_rndseq]
    jbe .wrapped
    mov ax, 1
.wrapped:
    mov [zf_rndctr], ax
    cmp ax, cx
    jbe .have
    mov ax, cx                      ; never answer outside the asked range
.have:
    mov bx, ax
    call zx_storeres
    ret
.real:
    call zx_rand16
    xor dx, dx
    div cx                          ; DX = 0..range-1
    inc dx
    mov bx, dx
    call zx_storeres
    ret

zxo_push:                           ; VAR:232 push value
    mov ax, [zf_ops]
    call zx_push
    ret

zxo_pull:                           ; VAR:233 pull (variable) / v6 pull stack
    cmp byte [zf_ver], 6
    je .v6
    call zx_pop
    mov bx, ax
    mov al, byte [zf_ops]
    call zx_setvar                  ; indirect (Standard 6.3.4)
    ret
.v6:
    cmp byte [zf_opn], 1
    jb .own
    ; a user stack (Standard 6.6): word 0 counts the SPARE slots
    mov ax, [zf_ops]
    call zm_gw16
    inc ax
    mov bx, ax
    mov ax, [zf_ops]
    call zm_pw16
    mov ax, bx
    shl ax, 1
    add ax, [zf_ops]
    call zm_gw16
    mov bx, ax
    call zx_storeres
    ret
.own:
    call zx_pop
    mov bx, ax
    call zx_storeres
    ret

zxo_split_window:                   ; VAR:234 split_window lines
    call zx_v6
    je .no                          ; v6 owns its windows outright
    mov ax, [zf_ops]
    push es
    call zw_split
    pop es
.no:
    ret

zxo_set_window:                     ; VAR:235 set_window window
    mov al, byte [zf_ops]
    call zx_v6
    je .v6
    push es
    call zw_setwin
    pop es
    ret
.v6:
    push es
    call zw6_select
    pop es
    ret

zxo_erase_window:                   ; VAR:237 erase_window window
    mov ax, [zf_ops]
    push es
    call zw_erasewin
    pop es
    ret

zxo_erase_line:                     ; VAR:238 erase_line value / v6 pixels
    ; v4/v5: only the value 1 means anything ("erase from the cursor to the
    ; end of the line"); anything else is defined as doing nothing.
    call zx_v6
    je .go
    cmp word [zf_ops], 1
    jne .no
.go:
    push es
    call zw_eraseline
    pop es
.no:
    ret

zxo_set_cursor:                     ; VAR:239 set_cursor line column [window]
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    call zx_v6
    je .v6
    push es
    call zw_cursor
    pop es
    ret
.v6:
    push es
    call zw6_setcur
    pop es
    ret

zxo_get_cursor:                     ; VAR:240 get_cursor array
    ; Two words into the array: row then column (Standard 15).
    call zx_v6
    je .v6
    push es
    call zw_getcur ; out AX = row, BX = column
    pop es
    jmp short .put
.v6:
    push es
    call zw6_getcur
    pop es
.put:
    push bx
    mov bx, ax
    mov ax, [zf_ops]
    call zm_pw16
    pop bx
    mov ax, [zf_ops]
    add ax, 2
    call zm_pw16
    ret

zxo_set_text_style:                 ; VAR:241 set_text_style style
    ; NAME CLASH, and it is real: zbss.inc already owns `zw_style` as the byte
    ; holding the live style bits, and a %define expands at the use site, so
    ; `call zw_style` would assemble into a call to that address. The routine
    ; is called zw_setstyle here; one of the two names has to move and the bss
    ; one is published in zbss.inc, so the routine is the one that gives way.
    mov al, byte [zf_ops]
    push es
    call zw_setstyle
    pop es
    ret

zxo_buffer_mode:                    ; VAR:242 buffer_mode flag
    ; The Standard lets an interpreter ignore this, and this one does: zwin
    ; word-wraps the lower window unconditionally because it is the only way
    ; the scrollback ring can hold whole lines, and turning wrapping off would
    ; make the scroll bar lie about how much text there is.
    ret

zxo_output_stream:                  ; VAR:243 output_stream number [table]
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    xor dx, dx
    push es
    call zt_stream ; ztext owns the four streams
    pop es
    ret

zxo_input_stream:                   ; VAR:244 input_stream number
    ; Stream 1 is "read commands from a file", which needs a file slot and
    ; therefore the UI task, for a feature no shipped story uses. Selecting it
    ; is ignored rather than refused: the keyboard stays selected and the game
    ; carries on, which is the degradation SPEC.md 47 asks for.
    ret

zxo_sound_effect:                   ; VAR:245 sound_effect number effect volume
    mov ax, [zf_ops]
    mov bl, 2                       ; default: start the effect
    cmp byte [zf_opn], 2
    jb .vol
    mov bl, byte [zf_ops+2]
.vol:
    mov bh, 8                       ; default volume, per the Standard's table
    cmp byte [zf_opn], 3
    jb .go
    mov bh, byte [zf_ops+4]
.go:
    push es
    call zs_effect
    pop es
    ret

zxo_read_char:                      ; VAR:246 read_char 1 time routine -> (result)
    call zx_spill
    call zx_unlock
    call zi_readchar                ; out AL = the character
    push ax
    call zx_lock
    pop ax
    call zx_load
    xor ah, ah
    mov bx, ax
    call zx_storeres
    ret

zxo_scan_table:                     ; VAR:247 scan_table x table len form
    mov ax, [zf_ops+6]
    cmp byte [zf_opn], 4
    jae .haveform
    mov ax, 0x82                    ; the default: words, 2 bytes apart
.haveform:
    mov [zx_tmp0], ax               ; form
    mov di, [zf_ops+2]              ; the running address
    mov cx, [zf_ops+4]              ; the field count
    jcxz .no
.l:
    mov ax, di
    test byte [zx_tmp0], 0x80
    jz .bytes
    call zm_gw16
    jmp short .cmp
.bytes:
    call zm_gb16
    xor ah, ah
.cmp:
    cmp ax, [zf_ops]
    je .found
    mov ax, [zx_tmp0]
    and ax, 0x7F
    add di, ax
    loop .l
.no:
    xor bx, bx
    call zx_storeres
    jmp zx_bf
.found:
    mov bx, di
    call zx_storeres
    jmp zx_bt

zxo_notv:                           ; VAR:248 not value -> (result), v5+
    mov bx, [zf_ops]
    not bx
    call zx_storeres
    ret

zxo_tokenise:                       ; VAR:251 tokenise text parse dict flag
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    xor cx, cx
    cmp byte [zf_opn], 3
    jb .flag
    mov cx, [zf_ops+4]              ; 0 = the story's own dictionary
.flag:
    xor dl, dl
    cmp byte [zf_opn], 4
    jb .go
    mov dl, byte [zf_ops+6]
.go:
    push es
    call zd_tokenise
    pop es
    ret

zxo_encode_text:                    ; VAR:252 encode_text zscii len from coded
    ; The text is in story memory and so is the answer, but zt_encode works in
    ; OUR segment - so this stages both ends through the bss. 40 bytes is the
    ; cap: a dictionary word is 6 or 9 encoded bytes, which is 9 characters at
    ; most, and nothing legitimate asks to encode more than that.
    mov cx, [zf_ops+2]
    cmp cx, 40
    jbe .len
    mov cx, 40
.len:
    mov [zx_tmp0], cx
    jcxz .none
    mov ax, [zf_ops]
    add ax, [zf_ops+4]              ; zscii-text + from
    mov di, zx_encbuf
.copyin:
    push cx
    push di
    push ax
    call zm_gb16
    pop dx
    pop di
    mov [di], al
    inc di
    mov ax, dx
    inc ax
    pop cx
    loop .copyin
.none:
    push si                         ; SI is the PC, borrowed for one call
    push es
    mov si, zx_encbuf
    mov cx, [zx_tmp0]
    mov di, zx_encout
    push es
    call zt_encode
    pop es
    pop es
    pop si
    ; 6 bytes of encoded text in v1-3, 9 from v4 (three Z-char words)
    mov cx, 6
    cmp byte [zf_ver], 4
    jb .wr
    mov cx, 9
.wr:
    mov di, [zf_ops+6]
    xor bx, bx
.wloop:
    mov al, [zx_encout+bx]
    push bx
    push cx
    push di
    mov bl, al
    mov ax, di
    call zm_pb16
    pop di
    pop cx
    pop bx
    inc di
    inc bx
    loop .wloop
    ret

zxo_copy_table:                     ; VAR:253 copy_table first second size
    ; Standard 15, including the correction in its own text: a POSITIVE size
    ; must be copied in whichever direction does not corrupt the source, and a
    ; NEGATIVE one must be copied forwards even when that does corrupt it -
    ; Beyond Zork fills an array with spaces that way, and Journey's menu
    ; breaks if the positive case always runs backwards.
    mov cx, [zf_ops+4]
    mov ax, [zf_ops+2]
    or ax, ax
    jnz .copy
    ; second == 0: zero `size` bytes of first
    jcxz .out
    mov di, [zf_ops]
    xor bx, bx
.zloop:
    push cx
    push di
    mov ax, di
    call zm_pb16
    pop di
    pop cx
    inc di
    loop .zloop
    ret
.copy:
    or cx, cx
    js .fwd                         ; negative size: forwards, always
    mov ax, [zf_ops]
    cmp ax, [zf_ops+2]
    ja .fwd                         ; source above destination: forwards is safe
    ; overlapping downwards: copy backwards, highest byte first
    jcxz .out
    mov di, cx
    dec di
.bloop:
    mov ax, [zf_ops]
    add ax, di
    push di
    call zm_gb16
    pop di
    mov bl, al
    mov ax, [zf_ops+2]
    add ax, di
    push di
    call zm_pb16
    pop di
    or di, di
    jz .out
    dec di
    jmp short .bloop
.fwd:
    or cx, cx
    jns .fok
    neg cx
.fok:
    jcxz .out
    xor di, di
.floop:
    mov ax, [zf_ops]
    add ax, di
    push cx
    push di
    call zm_gb16
    pop di
    mov bl, al
    mov ax, [zf_ops+2]
    add ax, di
    push di
    call zm_pb16
    pop di
    pop cx
    inc di
    loop .floop
.out:
    ret

zxo_print_table:                    ; VAR:254 print_table text width height skip
    mov cx, 1
    cmp byte [zf_opn], 3
    jb .h
    mov cx, [zf_ops+4]
.h:
    mov [zx_tmp0], cx               ; height
    xor ax, ax
    cmp byte [zf_opn], 4
    jb .s
    mov ax, [zf_ops+6]
.s:
    mov [zx_tmp1], ax               ; skip
    mov ax, [zf_ops]
    mov [zx_tmp2], ax               ; the running address
.rows:
    cmp word [zx_tmp0], 0
    je .out
    mov cx, [zf_ops+2]
    jcxz .endrow
.cols:
    push cx
    mov ax, [zx_tmp2]
    call zm_gb16
    inc word [zx_tmp2]
    push es
    call zt_putc
    pop es
    pop cx
    loop .cols
.endrow:
    mov ax, [zx_tmp1]
    add [zx_tmp2], ax
    dec word [zx_tmp0]
    jz .out
    mov al, 13
    push es
    call zt_putc
    pop es
    jmp short .rows
.out:
    ret

zxo_check_arg_count:                ; VAR:255 check_arg_count argument-number
    ; Standard 15: this is what makes routine(1) distinguishable from
    ; routine(1,0), so it counts what the CALL supplied, not what the routine
    ; declared - which is why ZXF_ARGC is in the frame at all.
    push es
    mov es, [zf_stkseg]
    mov bx, [zf_fp]
    mov ax, [es:bx+ZXF_ARGC]
    pop es
    cmp [zf_ops], ax
    jbe zx_bt
    jmp zx_bf

; =============================================================================
; EXT opcodes (v5 and later; the v6 half is the window and picture model)
;
; The v6 window opcodes all name the window they act on, while zwin6's API acts
; on the SELECTED one - so each of them selects, acts, and puts the selection
; back. [zw6_cur] is read to know what to put back; it is published in
; zbss.inc for exactly this, and there is no second copy of the selection.
; =============================================================================

; -----------------------------------------------------------------------------
; zx6_sel / zx6_unsel - borrow a v6 window for one operation
; in:  AL = the window number / AL = the one to put back
; out: zx6_sel leaves AH = the window that WAS selected; preserves all else
; -----------------------------------------------------------------------------
zx6_sel:
    mov ah, [zw6_cur]
    push es
    call zw6_select
    pop es
    ret
zx6_unsel:
    push es
    call zw6_select
    pop es
    ret

zxe_save:                           ; EXT:0  save table bytes name -> (result)
    ; The v5 form can save an arbitrary table to a named file. Frotz saves the
    ; game state and nothing else: an auxiliary-file save is a second file
    ; format for a feature no shipped story uses, and answering 0 is the
    ; Standard's own "this did not happen" (SPEC.md 47 - refuse, do not fake).
    cmp byte [zf_opn], 0
    ja .aux
    call zx_spill
    call zx_unlock
    call zi_save
    mov dl, 0
    jnc .ok
    mov dl, 1
.ok:
    push dx
    call zx_lock
    pop dx
    mov bx, 1
    or dl, dl
    jz .st
    xor bx, bx
.st:
    call zx_storeres
    ret
.aux:
    xor bx, bx
    call zx_storeres
    ret

zxe_restore:                        ; EXT:1  restore table bytes name -> (result)
    cmp byte [zf_opn], 0
    ja .aux
    call zx_spill
    call zx_unlock
    call zi_restore
    mov dl, 0
    jnc .ok
    mov dl, 1
.ok:
    push dx
    call zx_lock
    pop dx
    call zx_load                    ; on success this is the SAVED PC, which
    mov bx, 2                       ; points back at this same store byte
    or dl, dl
    jz .st
    xor bx, bx
.st:
    call zx_storeres
    ret
.aux:
    xor bx, bx
    call zx_storeres
    ret

zxe_log_shift:                      ; EXT:2  log_shift number places -> (result)
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    or bx, bx
    js .right
    mov cx, bx
    cmp cx, 16
    jbe .lok
    mov cx, 16                      ; the 8086 does not mask the count: a shift
.lok:                               ; of 200 would really take 200 iterations
    shl ax, cl
    jmp short .st
.right:
    neg bx
    mov cx, bx
    cmp cx, 16
    jbe .rok
    mov cx, 16
.rok:
    shr ax, cl
.st:
    mov bx, ax
    call zx_storeres
    ret

zxe_art_shift:                      ; EXT:3  art_shift number places -> (result)
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    or bx, bx
    js .right
    mov cx, bx
    cmp cx, 16
    jbe .lok
    mov cx, 16
.lok:
    shl ax, cl
    jmp short .st
.right:
    neg bx
    mov cx, bx
    cmp cx, 16
    jbe .rok
    mov cx, 16
.rok:
    sar ax, cl                      ; the sign is shifted on down, not zeros
.st:
    mov bx, ax
    call zx_storeres
    ret

zxe_set_font:                       ; EXT:4  set_font font -> (result)
    ; There is exactly one font in this kernel (SPEC.md 6: 8x8, copied out of
    ; the ROM, no slot loads a second one) and it is fixed-pitch, so font 1 and
    ; font 4 are both it and everything else answers 0 = "not available".
    ; Font 0 is the query form and changes nothing.
    mov ax, [zf_ops]
    or ax, ax
    jz .cur
    cmp ax, 1
    je .cur
    cmp ax, 4
    je .cur
    xor bx, bx
    call zx_storeres
    ret
.cur:
    mov bx, 1
    call zx_storeres
    ret

zxe_draw_picture:                   ; EXT:5  draw_picture picture-number y x
    mov ax, [zf_ops]
    mov dx, [zf_ops+2]
    mov cx, [zf_ops+4]
    push es
    call zp_draw
    pop es
    ret

zxe_picture_data:                   ; EXT:6  picture_data pic array ?(label)
    ; Standard 15: number 0 asks about the ARCHIVE - how many pictures and its
    ; release - and the branch says whether there are any at all. Answering
    ; truthfully when there is no picture file is required, and it is what
    ; lets a v6 story with its art missing degrade instead of failing.
    mov ax, [zf_ops]
    or ax, ax
    jz .archive
    push es
    call zp_info ; out CX = height, DX = width, CF = none
    pop es
    jc zx_bf
    push dx
    mov bx, cx
    mov ax, [zf_ops+2]
    call zm_pw16
    pop bx
    mov ax, [zf_ops+2]
    add ax, 2
    call zm_pw16
    jmp zx_bt
.archive:
    push es
    call zp_count_get ; out AX = count, BX = release
    pop es
    push ax
    push bx
    mov bx, ax
    mov ax, [zf_ops+2]
    call zm_pw16
    pop bx
    mov ax, [zf_ops+2]
    add ax, 2
    call zm_pw16
    pop ax
    or ax, ax
    jz zx_bf
    jmp zx_bt

zxe_erase_picture:                  ; EXT:7  erase_picture picture-number y x
    ; Not implemented: it wants a background-coloured rectangle the size of a
    ; picture, and zwin6 owns every rectangle in a v6 window. Doing nothing
    ; leaves the picture on screen, which is wrong but stable; drawing over it
    ; from here would be wrong AND would fight zwin6 for the same pixels.
    ret

zxe_set_margins:                    ; EXT:8  set_margins left right window
    mov al, byte [zf_ops+4]
    call zx6_sel
    push ax
    mov ax, [zf_ops]
    mov bx, [zf_ops+2]
    push es
    call zw6_margins
    pop es
    pop ax
    mov al, ah
    call zx6_unsel
    ret

zxe_save_undo:                      ; EXT:9  save_undo -> (result)
    ; The snapshot claim is optional (FROTZ-PLAN 3: a story that loses UNDO on
    ; a small machine is better than one that will not start), so a refusal is
    ; a normal answer here, not an error.
    call zx_spill                   ; the PC a later @restore_undo comes back to
    call zi_save_undo
    mov bx, 1
    jnc .st
    xor bx, bx
.st:
    call zx_storeres
    ret

zxe_restore_undo:                   ; EXT:10 restore_undo -> (result)
    call zx_spill
    call zi_restore_undo
    mov dl, 0
    jnc .ok
    mov dl, 1
.ok:
    call zx_load                    ; on success this is @save_undo's own PC
    mov bx, 2
    or dl, dl
    jz .st
    xor bx, bx
.st:
    call zx_storeres
    ret

zxe_print_unicode:                  ; EXT:11 print_unicode char-number
    ; The font draws 32..126 and nothing else (SPEC.md 6), so a code above 255
    ; cannot be approximated at all and prints as '?'. Everything below goes
    ; to ztext, which owns the ZSCII-to-glyph approximation for the accented
    ; range.
    mov ax, [zf_ops]
    cmp ax, 255
    jbe .ok
    mov al, '?'
.ok:
    push es
    call zt_putc
    pop es
    ret

zxe_check_unicode:                  ; EXT:12 check_unicode char -> (result)
    ; Bit 0 = we can print it, bit 1 = we can receive it. Both are true for
    ; the printable ASCII the ROM font carries and neither is true for
    ; anything else - saying otherwise would have stories print text that
    ; comes out as blanks.
    mov ax, [zf_ops]
    cmp ax, 32
    jb .no
    cmp ax, 126
    ja .no
    mov bx, 3
    call zx_storeres
    ret
.no:
    xor bx, bx
    call zx_storeres
    ret

zxe_set_true_colour:                ; EXT:13 set_true_colour fg bg [window]
    ; 15-bit RGB against a 16-entry palette that is black and white on two
    ; adapters of three. Ignored, deliberately: the Standard's own -1/-2
    ; defaults mean "the interpreter's choice", and taking that choice is
    ; better than mapping a story's careful colour scheme onto a dither.
    ret

zxe_move_window:                    ; EXT:16 move_window window y x
    mov al, byte [zf_ops]
    call zx6_sel
    push ax
    mov ax, [zf_ops+2]
    mov bx, [zf_ops+4]
    push es
    call zw6_move
    pop es
    pop ax
    mov al, ah
    call zx6_unsel
    ret

zxe_window_size:                    ; EXT:17 window_size window y x
    mov al, byte [zf_ops]
    call zx6_sel
    push ax
    mov ax, [zf_ops+2]              ; y = height, x = width
    mov bx, [zf_ops+4]
    push es
    call zw6_size
    pop es
    pop ax
    mov al, ah
    call zx6_unsel
    ret

zxe_window_style:                   ; EXT:18 window_style window flags operation
    mov al, byte [zf_ops]
    call zx6_sel
    push ax
    mov ax, [zf_ops+2]
    mov bx, [zf_ops+4]
    push es
    call zw6_attr
    pop es
    pop ax
    mov al, ah
    call zx6_unsel
    ret

zxe_get_wind_prop:                  ; EXT:19 get_wind_prop window prop -> (result)
    mov al, byte [zf_ops]
    mov bl, byte [zf_ops+2]
    push es
    call zw6_getprop ; out AX
    pop es
    mov bx, ax
    call zx_storeres
    ret

zxe_scroll_window:                  ; EXT:20 scroll_window window pixels
    mov al, byte [zf_ops]
    call zx6_sel
    push ax
    mov ax, [zf_ops+2]
    push es
    call zw6_scroll
    pop es
    pop ax
    mov al, ah
    call zx6_unsel
    ret

zxe_pop_stack:                      ; EXT:21 pop_stack items stack
    cmp byte [zf_opn], 2
    jb .own
    ; a user stack (Standard 6.6): give the slots back by counting them up
    mov ax, [zf_ops+2]
    call zm_gw16
    add ax, [zf_ops]
    mov bx, ax
    mov ax, [zf_ops+2]
    call zm_pw16
    ret
.own:
    mov cx, [zf_ops]
    jcxz .out
.l:
    push cx
    call zx_pop
    pop cx
    loop .l
.out:
    ret

zxe_read_mouse:                     ; EXT:22 read_mouse array
    push es
    call zw6_mouse ; out AX = y, BX = x, CX = buttons
    pop es
    push cx
    push bx
    push ax
    mov bx, ax
    mov ax, [zf_ops]
    call zm_pw16
    pop ax
    pop bx
    mov ax, [zf_ops]
    add ax, 2
    call zm_pw16
    pop bx
    mov ax, [zf_ops]
    add ax, 4
    call zm_pw16
    mov ax, [zf_ops]                ; word 3 is the menu word; no story menus
    add ax, 6
    xor bx, bx
    call zm_pw16
    ret

zxe_mouse_window:                   ; EXT:23 mouse_window window
    ; Confines the mouse to one window. The kernel owns the pointer and there
    ; is no slot that constrains it, so this is a no-op rather than a lie.
    ret

zxe_push_stack:                     ; EXT:24 push_stack value stack ?(label)
    mov ax, [zf_ops+2]
    call zm_gw16                    ; word 0 = the spare slots left
    or ax, ax
    jz zx_bf                        ; full: the branch is how the story knows
    push ax
    shl ax, 1
    add ax, [zf_ops+2]
    mov bx, [zf_ops]
    call zm_pw16
    pop bx
    dec bx
    mov ax, [zf_ops+2]
    call zm_pw16
    jmp zx_bt

zxe_put_wind_prop:                  ; EXT:25 put_wind_prop window prop value
    mov al, byte [zf_ops]
    mov bl, byte [zf_ops+2]
    mov cx, [zf_ops+4]
    push es
    call zw6_putprop
    pop es
    ret

zxe_print_form:                     ; EXT:26 print_form formatted-table
    ; Standard 8.6.2's shape: a word giving a line's length, then that many
    ; ZSCII characters, repeated until a zero length ends the table.
    mov ax, [zf_ops]
    mov [zx_tmp2], ax
.line:
    mov ax, [zx_tmp2]
    call zm_gw16
    or ax, ax
    jz .out
    mov cx, ax
    add word [zx_tmp2], 2
.chars:
    push cx
    mov ax, [zx_tmp2]
    call zm_gb16
    inc word [zx_tmp2]
    push es
    call zt_putc
    pop es
    pop cx
    loop .chars
    mov al, 13
    push es
    call zt_putc
    pop es
    jmp short .line
.out:
    ret

zxe_make_menu:                      ; EXT:27 make_menu number table ?(label)
    ; The menu bar belongs to os8088, not to the story: a package's menu set is
    ; installed once with OSAPI_MENU_SET and the kernel draws it. Branching
    ; false is the Standard's "this interpreter cannot" and Journey copes.
    jmp zx_bf

zxe_picture_table:                  ; EXT:28 picture_table table
    ; A hint that some pictures are about to be used. The whole archive is
    ; resident already, so there is nothing to prepare.
    ret

zxe_buffer_screen:                  ; EXT:29 buffer_screen mode -> (result)
    xor bx, bx                      ; 0 = we do not buffer the screen
    call zx_storeres
    ret

; =============================================================================
; The dispatch tables (Standard 14)
;
; One table per operand count, every slot filled, so a decode can never fall
; through into whatever follows: an opcode number the version does not define
; lands on zx_bad, which halts with a message rather than executing wild.
; =============================================================================
zx_t2op:
    dw zx_bad,      zxo_je,         zxo_jl,         zxo_jg
    dw zxo_dec_chk, zxo_inc_chk,    zxo_jin,        zxo_test
    dw zxo_or,      zxo_and,        zxo_test_attr,  zxo_set_attr
    dw zxo_clear_attr, zxo_store,   zxo_insert_obj, zxo_loadw
    dw zxo_loadb,   zxo_get_prop,   zxo_get_prop_addr, zxo_get_next_prop
    dw zxo_add,     zxo_sub,        zxo_mul,        zxo_div
    dw zxo_mod,     zxo_call_2s,    zxo_call_2n,    zxo_set_colour
    dw zxo_throw,   zx_bad,         zx_bad,         zx_bad

zx_t1op:
    dw zxo_jz,      zxo_get_sibling, zxo_get_child, zxo_get_parent
    dw zxo_get_prop_len, zxo_inc,   zxo_dec,        zxo_print_addr
    dw zxo_call_1s, zxo_remove_obj, zxo_print_obj,  zxo_ret
    dw zxo_jump,    zxo_print_paddr, zxo_load,      zxo_not1

zx_t0op:
    dw zxo_rtrue,   zxo_rfalse,     zxo_print,      zxo_print_ret
    dw zxo_nop,     zxo_save0,      zxo_restore0,   zxo_restart
    dw zxo_ret_popped, zxo_pop_catch, zxo_quit,     zxo_new_line
    dw zxo_show_status, zxo_verify, zx_bad,         zxo_piracy

zx_tvar:
    dw zxo_call,    zxo_storew,     zxo_storeb,     zxo_put_prop
    dw zxo_read,    zxo_print_char, zxo_print_num,  zxo_random
    dw zxo_push,    zxo_pull,       zxo_split_window, zxo_set_window
    dw zxo_call,    zxo_erase_window, zxo_erase_line, zxo_set_cursor
    dw zxo_get_cursor, zxo_set_text_style, zxo_buffer_mode, zxo_output_stream
    dw zxo_input_stream, zxo_sound_effect, zxo_read_char, zxo_scan_table
    dw zxo_notv,    zxo_call_vn,    zxo_call_vn,    zxo_tokenise
    dw zxo_encode_text, zxo_copy_table, zxo_print_table, zxo_check_arg_count

zx_text:
    dw zxe_save,    zxe_restore,    zxe_log_shift,  zxe_art_shift
    dw zxe_set_font, zxe_draw_picture, zxe_picture_data, zxe_erase_picture
    dw zxe_set_margins, zxe_save_undo, zxe_restore_undo, zxe_print_unicode
    dw zxe_check_unicode, zxe_set_true_colour, zx_bad, zx_bad
    dw zxe_move_window, zxe_window_size, zxe_window_style, zxe_get_wind_prop
    dw zxe_scroll_window, zxe_pop_stack, zxe_read_mouse, zxe_mouse_window
    dw zxe_push_stack, zxe_put_wind_prop, zxe_print_form, zxe_make_menu
    dw zxe_picture_table, zxe_buffer_screen, zx_bad, zx_bad

%ifdef ZXTRACE
; =============================================================================
; zx_diag - dump the header cache and the first bytes at the PC (ZXTRACE only)
; =============================================================================
; A build-time switch, not a shipped feature: `make ztest ZXTRACE=1`. It exists
; because the first thing this interpreter did on real story was halt with a
; stack overflow before printing anything, and the difference between "the
; header parsed wrong" and "the decoder ran off the rails" is four numbers.
zx_diag:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    mov si, zxd_v
    push es
    call zt_puts
    pop es
    mov al, [zf_ver]
    xor ah, ah
    push es
    call zt_num
    pop es
    mov si, zxd_pc
    push es
    call zt_puts
    pop es
    mov ax, [zf_initpc]
    call zx_hex
    mov si, zxd_st
    push es
    call zt_puts
    pop es
    mov ax, [zf_static]
    call zx_hex
    mov si, zxd_hi
    push es
    call zt_puts
    pop es
    mov ax, [zf_himem]
    call zx_hex
    mov si, zxd_ob
    push es
    call zt_puts
    pop es
    mov ax, [zf_objtab]
    call zx_hex
    mov si, zxd_gv
    push es
    call zt_puts
    pop es
    mov ax, [zf_gvar]
    call zx_hex
    mov si, zxd_by
    push es
    call zt_puts
    pop es
    mov cx, 8
    mov ax, [zf_initpc]
    mov bx, ax
.bytes:
    push cx
    mov ax, bx
    call zm_gb16
    xor ah, ah
    call zx_hex2
    mov al, ' '
    push es
    call zt_putc
    pop es
    inc bx
    pop cx
    loop .bytes
    mov al, 13
    push es
    call zt_putc
    pop es
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

; AL as two hex digits.
zx_hex2:
    push cx
    mov cl, 4
    shl ax, cl
    shl ax, cl
    call zx_hex4lo
    pop cx
    ret
zx_hex4lo:
    push ax
    push cx
    mov cx, 2
    mov dx, ax
.d:
    push cx
    mov cl, 4
    rol dx, cl
    mov al, dl
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .ok
    add al, 7
.ok:
    push es
    call zt_putc
    pop es
    pop cx
    loop .d
    pop cx
    pop ax
    ret

zxd_v:  db 'v', 0
zxd_pc: db ' pc=', 0
zxd_st: db ' st=', 0
zxd_hi: db ' hi=', 0
zxd_ob: db ' ob=', 0
zxd_gv: db ' gv=', 0
zxd_by: db 13, 'bytes: ', 0
%endif

; zx_hex / zx_hex2 are NOT trace-only: a halt reports the opcode and the
; address, because "unknown opcode" without either sends the reader to a
; disassembler, and the whole point of stopping cleanly is to say what stopped.
; AX as four hex digits.
zx_hex:
    push ax
    push cx
    push dx
    mov dx, ax
    mov cx, 4
.d:
    push cx
    mov cl, 4
    rol dx, cl
    mov al, dl
    and al, 15
    add al, '0'
    cmp al, '9'
    jbe .ok
    add al, 7
.ok:
    push es
    call zt_putc
    pop es
    pop cx
    loop .d
    pop dx
    pop cx
    pop ax
    ret

%ifdef ZXTRACE
; zx_trace - one line per instruction for the first ZXT_N of them.
; ES:SI is the live PC. Prints "seg:off op" and nothing else - enough to see
; whether the PC advances and what the decoder is being handed, which is the
; whole question when a story halts before printing anything.
ZXT_N equ 14
ZXT_MASK equ 0x0FFF
%define zxt_n (os88_image_end + ZEXEC_SLACK + 400)   ; word, inside our slack
zx_trace:
    push ax
    push bx
    push cx
    push dx
    push si
    push es
    inc word [zxt_n]
    mov ax, [zxt_n]
    and ax, ZXT_MASK            ; one sample every ZXT_MASK+1 instructions, for
    jnz .out                    ; ever: a loop shows as the same few addresses
                                ; repeating, which is the whole question
    mov ax, es
    push es
    push si
    call zx_hex
    mov al, ':'
    push es
    call zt_putc
    pop es
    pop si
    pop es
    push es
    push si
    mov ax, si
    call zx_hex
    mov al, ' '
    push es
    call zt_putc
    pop es
    pop si
    pop es
    mov al, [es:si]
    xor ah, ah
    push es
    push si
    call zx_hex2
    mov al, 13
    push es
    call zt_putc
    pop es
    pop si
    pop es
.out:
    pop es
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

%endif
