; =============================================================================
; os8088 - apps/frotz/zexec.inc
;
; The Z-machine itself: instruction decode (Standard 4), the call frames and
; the Z-stack (Standard 6), and every opcode in the table (Standard 15). Every
; other module in Frotz exists to be called from here.
;
; THREE DECISIONS SHAPE THIS FILE.
;
;  - **The PC is a live ES:SI and stays one.** docs/FROTZ-PLAN.md 3.1: a story
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
ZX_STKBYTES equ 16384

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
    push cx
    call zx_step
    pop cx
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
    push ax
    mov ax, es
    mov [zf_pcseg], ax
    mov [zf_pcoff], si
    pop ax
    ret
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
    cmp si, 0x8000
    jb .norm
    call zm_norm                    ; once per 32KB of straight-line code
.norm:
    mov byte [zf_opn], 0
    mov byte [zf_store], 0xFF
    mov al, [es:si]
    inc si

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
; -----------------------------------------------------------------------------
zx_jrel:
    or bx, bx
    js .neg
    add si, bx
    ret
.neg:
    add si, bx
    jc .ok
    push ax
    mov ax, es
    sub ax, 0x1000
    mov es, ax
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
    rep stosw                       ; ES:DI is the frame; DF is clear by contract

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
    call zm_seek
    jmp .out

; -----------------------------------------------------------------------------
; zx_ret - pop the current frame and return a value
; in:  AX = the return value
; out: ES:SI = the caller's PC; preserves every register but ES:SI
;
; Standard 6.3.2 makes this cheap: everything the routine pushed is discarded
; by setting the stack pointer back to the frame base, so there is no unwind
; loop and no way for a routine to leak stack into its caller.
; -----------------------------------------------------------------------------
zx_ret:
    push ax
    push bx
    push cx
    push dx
    push di
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
    call zm_seek
    mov cl, [zx_tmp3]
    cmp cl, 0xFF
    je .done
    mov bx, [zf_sp]                 ; scratch; the value is on our own stack
    pop di
    pop dx
    pop cx
    pop bx
    pop ax                          ; AX = the return value again
    push ax
    push bx
    push cx
    push dx
    push di
    mov bx, ax
    mov al, [zx_tmp3]
    call zx_store
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
zx_bad:
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
    call zw_notice
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
zx_m_locals: db '[Frotz: bad routine header - the story has stopped.]', 0
zx_m_ret:    db '[Frotz: return with no caller - the story has stopped.]', 0
zx_m_frame:  db '[Frotz: bad stack frame - the story has stopped.]', 0
zx_m_other:  db '[Frotz: the story has stopped.]', 0
